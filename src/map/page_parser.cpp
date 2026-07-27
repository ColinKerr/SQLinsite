#include "map/page_parser.hpp"

#include "map/sqlite_format.hpp"

namespace page_parser {
namespace {

using sqlfmt::Byte;

PageType btreeType(Byte type) {
    switch (type) {
        case 13: return PageType::TableLeaf;
        case 5:  return PageType::TableInterior;
        case 10: return PageType::IndexLeaf;
        case 2:  return PageType::IndexInterior;
        default: return PageType::Unallocated;
    }
}

std::int64_t computeFreeBytes(const std::vector<Byte>& page,
                              const sqlfmt::BtreeHeader& bh, int headerStart) {
    const int pageSize = static_cast<int>(page.size());
    const int contentStart =
        bh.cellContentStart == 0 ? 65536 : bh.cellContentStart;
    const int cellArrayEnd = headerStart + bh.headerSize + 2 * bh.cellCount;
    std::int64_t gap = contentStart - cellArrayEnd;
    if (gap < 0) gap = 0;

    std::int64_t freeblocks = 0;
    int addr = bh.firstFreeblock;
    int guard = 0;
    while (addr != 0 && addr + 4 <= pageSize && guard++ < pageSize) {
        const int size = static_cast<int>(sqlfmt::readBE16(page.data() + addr + 2));
        freeblocks += size;
        addr = static_cast<int>(sqlfmt::readBE16(page.data() + addr));
    }
    return gap + freeblocks + bh.fragmentedFreeBytes;
}

}  // namespace

PageInfo parseBtree(const DbFile& db, std::int64_t pageNumber) {
    const std::vector<Byte>& page = db.page(pageNumber);
    const int pageSize = static_cast<int>(page.size());
    const int usable = db.usableSize();
    const int headerStart = (pageNumber == 1) ? 100 : 0;

    PageInfo info;
    info.pageNumber = pageNumber;

    const sqlfmt::BtreeHeader bh =
        sqlfmt::parseBtreeHeader(page.data(), headerStart);
    info.type = btreeType(bh.type);
    if (info.type == PageType::Unallocated) {
        info.parseError = "invalid b-tree page type byte";
        return info;
    }

    info.header.firstFreeblock = bh.firstFreeblock;
    info.header.cellCount = bh.cellCount;
    info.header.cellContentStart =
        bh.cellContentStart == 0 ? 65536 : bh.cellContentStart;
    info.header.fragmentedFreeBytes = bh.fragmentedFreeBytes;
    if (bh.rightmostPointer >= 0) {
        info.header.rightmostPointer = bh.rightmostPointer;
        info.pointers.push_back({bh.rightmostPointer, "child"});
    }
    info.freeBytes = computeFreeBytes(page, bh, headerStart);

    const bool tableLeaf = info.type == PageType::TableLeaf;
    const bool indexLeaf = info.type == PageType::IndexLeaf;
    const bool tableInterior = info.type == PageType::TableInterior;
    const bool indexInterior = info.type == PageType::IndexInterior;

    const int cellArrayStart = headerStart + bh.headerSize;
    for (int i = 0; i < bh.cellCount; ++i) {
        const int ptrOffset = cellArrayStart + 2 * i;
        if (ptrOffset + 2 > pageSize) {
            info.parseError = "cell pointer array out of bounds";
            break;
        }
        int cell = static_cast<int>(sqlfmt::readBE16(page.data() + ptrOffset));
        if (cell < 0 || cell >= pageSize) {
            info.parseError = "cell offset out of bounds";
            break;
        }

        CellInfo c;
        const Byte* p = page.data() + cell;
        std::size_t remaining = static_cast<std::size_t>(pageSize - cell);

        if (tableInterior) {
            // Interior cells carry a 4-byte left-child pointer and an integer
            // divider key. That key is a b-tree boundary (it can even be stale
            // after deletions), NOT a real row's rowid — rowid is a table-leaf-only
            // concept, so it is deliberately left unset here.
            c.leftChild = sqlfmt::readBE32(p);
            info.pointers.push_back({*c.leftChild, "child"});
        } else if (tableLeaf) {
            const sqlfmt::Varint payload = sqlfmt::readVarint(p, remaining);
            const sqlfmt::Varint rowid =
                sqlfmt::readVarint(p + payload.length, remaining - payload.length);
            c.payloadBytes = static_cast<std::int64_t>(payload.value);
            c.rowid = static_cast<std::int64_t>(rowid.value);
            const sqlfmt::PayloadSplit split =
                sqlfmt::localPayload(payload.value, usable, true);
            c.localBytes = static_cast<std::int64_t>(split.localBytes);
            if (split.hasOverflow) {
                const int ovOffset =
                    cell + payload.length + rowid.length + static_cast<int>(split.localBytes);
                if (ovOffset + 4 <= pageSize) {
                    c.overflowPage = sqlfmt::readBE32(page.data() + ovOffset);
                    info.pointers.push_back({*c.overflowPage, "overflow"});
                }
            }
        } else {  // index leaf or interior
            int keyStart = cell;
            if (indexInterior) {
                c.leftChild = sqlfmt::readBE32(p);
                info.pointers.push_back({*c.leftChild, "child"});
                keyStart += 4;
            }
            const Byte* kp = page.data() + keyStart;
            const std::size_t kremain = static_cast<std::size_t>(pageSize - keyStart);
            const sqlfmt::Varint payload = sqlfmt::readVarint(kp, kremain);
            c.payloadBytes = static_cast<std::int64_t>(payload.value);
            const sqlfmt::PayloadSplit split =
                sqlfmt::localPayload(payload.value, usable, false);
            c.localBytes = static_cast<std::int64_t>(split.localBytes);
            // Index key fields are NOT decoded/stored in the map: the map records
            // only the cell's structure (payload/local bytes, overflow). The Page
            // Detail view decodes keys on demand from the source db (see
            // page_content.cpp / the /content endpoint), so persisting them here
            // would be dead weight — index keyJson was the bulk of map size + build
            // time. See PAGES_AND_TABLES_VIEWS / map perf notes.
            const int payloadStart = keyStart + payload.length;
            if (split.hasOverflow) {
                const int ovOffset = payloadStart + static_cast<int>(split.localBytes);
                if (ovOffset + 4 <= pageSize) {
                    c.overflowPage = sqlfmt::readBE32(page.data() + ovOffset);
                    info.pointers.push_back({*c.overflowPage, "overflow"});
                }
            }
            (void)indexLeaf;
        }

        info.cells.push_back(std::move(c));
    }

    return info;
}

PageInfo parseOverflow(const DbFile& db, std::int64_t pageNumber) {
    const std::vector<Byte>& page = db.page(pageNumber);
    PageInfo info;
    info.pageNumber = pageNumber;
    info.type = PageType::Overflow;
    const std::int64_t next = sqlfmt::readBE32(page.data());
    if (next != 0) {
        info.header.nextOverflowPage = next;
        info.pointers.push_back({next, "overflow"});
    }
    return info;
}

PageInfo parseFreelistTrunk(const DbFile& db, std::int64_t pageNumber) {
    const std::vector<Byte>& page = db.page(pageNumber);
    const int pageSize = static_cast<int>(page.size());
    PageInfo info;
    info.pageNumber = pageNumber;
    info.type = PageType::FreelistTrunk;

    const std::int64_t next = sqlfmt::readBE32(page.data());
    int leafCount = static_cast<int>(sqlfmt::readBE32(page.data() + 4));
    const int maxLeaves = (pageSize - 8) / 4;
    if (leafCount > maxLeaves) {
        leafCount = maxLeaves;
        info.parseError = "freelist leaf count exceeds page capacity";
    }
    if (next != 0) {
        info.header.nextTrunkPage = next;
        info.pointers.push_back({next, "freelist-next"});
    }
    info.header.leafCount = leafCount;
    for (int i = 0; i < leafCount; ++i) {
        const std::int64_t leaf = sqlfmt::readBE32(page.data() + 8 + 4 * i);
        info.pointers.push_back({leaf, "freelist-leaf"});
    }
    return info;
}

PageInfo parsePointerMap(const DbFile& db, std::int64_t pageNumber) {
    const std::vector<Byte>& page = db.page(pageNumber);
    PageInfo info;
    info.pageNumber = pageNumber;
    info.type = PageType::PointerMap;

    const int entriesPerPage = db.usableSize() / 5;
    const std::int64_t pageCount = db.pageCount();
    for (int k = 0; k < entriesPerPage; ++k) {
        const std::int64_t target = pageNumber + 1 + k;
        if (target > pageCount) break;
        const Byte* e = page.data() + 5 * k;
        PtrmapEntry entry;
        entry.targetPage = target;
        entry.entryType = e[0];
        entry.parentPage = sqlfmt::readBE32(e + 1);
        if (entry.parentPage != 0) {
            info.pointers.push_back({entry.parentPage, "ptrmap-parent"});
        }
        info.ptrmapEntries.push_back(entry);
    }
    return info;
}

PageInfo parseSimple(std::int64_t pageNumber, PageType type) {
    PageInfo info;
    info.pageNumber = pageNumber;
    info.type = type;
    return info;
}

}  // namespace page_parser
