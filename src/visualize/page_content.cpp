#include "visualize/page_content.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "map/sqlite_format.hpp"

using nlohmann::json;
using sqlfmt::Byte;

namespace {

constexpr std::size_t kValueCap = 65536;      // max text chars emitted per value
constexpr std::size_t kMaxAssemble = 1 << 20; // cap on assembled record bytes

// English name for a record serial type code (SQLite fileformat §2.1).
std::string serialName(std::uint64_t st) {
    switch (st) {
        case 0: return "NULL";
        case 1: return "int8";
        case 2: return "int16";
        case 3: return "int24";
        case 4: return "int32";
        case 5: return "int48";
        case 6: return "int64";
        case 7: return "float64";
        case 8: return "int 0";
        case 9: return "int 1";
        case 10: case 11: return "reserved";
        default:
            return (st % 2 == 0) ? ("blob(" + std::to_string((st - 12) / 2) + ")")
                                 : ("text(" + std::to_string((st - 13) / 2) + ")");
    }
}

// English name for a b-tree page header type byte.
std::string btreeTypeName(int type) {
    switch (type) {
        case 2: return "index interior";
        case 5: return "table interior";
        case 10: return "index leaf";
        case 13: return "table leaf";
        default: return "unknown";
    }
}

// One decoded record column value → JSON (value text capped to kValueCap chars).
json valueJson(std::uint64_t serialType, const sqlfmt::CellValue& v) {
    json j = {{"serialType", serialType}, {"serialName", serialName(serialType)}};
    switch (v.type) {
        case sqlfmt::CellValue::Type::Null: j["type"] = "null"; j["value"] = nullptr; break;
        case sqlfmt::CellValue::Type::Int:  j["type"] = "int";  j["value"] = v.intValue; break;
        case sqlfmt::CellValue::Type::Real: j["type"] = "real"; j["value"] = v.realValue; break;
        case sqlfmt::CellValue::Type::Text: {
            j["type"] = "text"; j["bytes"] = v.byteSize;
            if (v.text.size() > kValueCap) { j["value"] = v.text.substr(0, kValueCap); j["truncated"] = true; }
            else j["value"] = v.text;
            break;
        }
        case sqlfmt::CellValue::Type::Blob:
            j["type"] = "blob"; j["value"] = nullptr; j["bytes"] = v.byteSize; break;
    }
    if (v.truncated) j["truncated"] = true;
    return j;
}

// Sorts regions by offset and fills the gaps between them with `free` spans (and
// a trailing `reserved` span for reserved-bytes-per-page), so the schematic
// accounts for every byte of the page.
json fillGaps(std::vector<json>& regions, int usableSize, int pageSize) {
    std::sort(regions.begin(), regions.end(), [](const json& a, const json& b) {
        return a["offset"].get<std::int64_t>() < b["offset"].get<std::int64_t>();
    });
    json out = json::array();
    std::int64_t cursor = 0;
    for (json& r : regions) {
        // Read fields before moving r into the output.
        const std::int64_t off = r["offset"].get<std::int64_t>();
        const std::int64_t len = r["length"].get<std::int64_t>();
        if (off > cursor) out.push_back({{"offset", cursor}, {"length", off - cursor}, {"kind", "free"}});
        cursor = std::max(cursor, off + len);
        out.push_back(std::move(r));
    }
    if (cursor < usableSize) {
        out.push_back({{"offset", cursor}, {"length", usableSize - cursor}, {"kind", "free"}});
    }
    if (usableSize < pageSize) {
        out.push_back({{"offset", usableSize}, {"length", pageSize - usableSize}, {"kind", "reserved"}});
    }
    return out;
}

bool isBtree(const std::string& t) {
    return t == "table-leaf" || t == "table-interior" || t == "index-leaf" || t == "index-interior";
}

}  // namespace

PageContent PageContent::open(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string msg = db ? sqlite3_errmsg(db) : "cannot open database";
        sqlite3_close(db);
        throw std::runtime_error("page_content: " + msg);
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT data FROM sqlite_dbpage('main') WHERE pgno=?1", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw std::runtime_error("page_content: " + msg);
    }
    PageContent p;
    p.db_ = db;
    p.pageStmt_ = stmt;
    const std::vector<Byte> page1 = p.readPage(1);
    if (page1.size() < 100) throw std::runtime_error("page_content: not a SQLite database");
    const std::uint32_t stored = sqlfmt::readBE16(page1.data() + 16);
    p.pageSize_ = stored == 1 ? 65536 : static_cast<int>(stored);
    p.usableSize_ = p.pageSize_ - page1[20];
    return p;
}

void PageContent::close() noexcept {
    if (pageStmt_ != nullptr) sqlite3_finalize(pageStmt_);
    if (db_ != nullptr) sqlite3_close(db_);
    pageStmt_ = nullptr;
    db_ = nullptr;
}

PageContent::~PageContent() { close(); }

PageContent::PageContent(PageContent&& o) noexcept
    : db_(o.db_), pageStmt_(o.pageStmt_), pageSize_(o.pageSize_), usableSize_(o.usableSize_) {
    o.db_ = nullptr;
    o.pageStmt_ = nullptr;
}

PageContent& PageContent::operator=(PageContent&& o) noexcept {
    if (this != &o) {
        close();
        db_ = o.db_;
        pageStmt_ = o.pageStmt_;
        pageSize_ = o.pageSize_;
        usableSize_ = o.usableSize_;
        o.db_ = nullptr;
        o.pageStmt_ = nullptr;
    }
    return *this;
}

std::vector<std::uint8_t> PageContent::readPage(std::int64_t pageNumber) const {
    std::vector<std::uint8_t> out;
    if (pageNumber < 1 || pageStmt_ == nullptr) return out;
    sqlite3_reset(pageStmt_);
    sqlite3_bind_int64(pageStmt_, 1, pageNumber);
    if (sqlite3_step(pageStmt_) == SQLITE_ROW) {
        const auto* bytes = static_cast<const std::uint8_t*>(sqlite3_column_blob(pageStmt_, 0));
        const int len = sqlite3_column_bytes(pageStmt_, 0);
        if (bytes != nullptr && len > 0) out.assign(bytes, bytes + len);
    }
    sqlite3_reset(pageStmt_);
    return out;
}

std::string PageContent::pageJson(std::int64_t n, const std::string& pageType,
                                  const std::function<std::string(std::int64_t)>& pageTypeOf) const {
    const std::vector<Byte> page = readPage(n);
    if (page.empty()) return {};
    const auto pageEnd = page.size();
    const std::uint64_t usable = static_cast<std::uint64_t>(usableSize_);

    std::vector<json> regions;
    json cells = json::array();
    json pointers = json::array();
    json header = json::object();

    auto readBE16 = [&](std::size_t o) { return o + 2 <= pageEnd ? sqlfmt::readBE16(page.data() + o) : 0; };
    auto readBE32 = [&](std::size_t o) {
        return o + 4 <= pageEnd ? sqlfmt::readBE32(page.data() + o) : 0;
    };
    // Bounded varint read: never forms an out-of-range pointer or maxLen.
    auto varintAt = [&](std::size_t o) {
        const std::size_t clamped = std::min(o, pageEnd);
        return sqlfmt::readVarint(page.data() + clamped, pageEnd - clamped);
    };

    // Assembles a cell's full record (local bytes + overflow chain, capped),
    // decodes its columns, and annotates each column whose bytes live (partly)
    // in the overflow pages with the overflow page numbers that hold them.
    auto decodeRecord = [&](std::size_t recStart, std::uint64_t localBytes,
                            std::uint64_t payloadBytes, std::int64_t firstOverflow) -> json {
        const std::uint64_t chunk = usable > 4 ? usable - 4 : 1;
        std::vector<Byte> buf;
        const std::size_t localTake = std::min<std::size_t>(
            static_cast<std::size_t>(localBytes), recStart < pageEnd ? pageEnd - recStart : 0);
        buf.insert(buf.end(), page.data() + recStart, page.data() + recStart + localTake);
        std::vector<std::int64_t> ovPages;
        std::int64_t cur = firstOverflow;
        const std::size_t cap = std::min<std::size_t>(static_cast<std::size_t>(payloadBytes), kMaxAssemble);
        while (buf.size() < cap && cur > 0) {
            ovPages.push_back(cur);
            const std::vector<Byte> op = readPage(cur);
            if (op.size() <= 4) break;
            const std::size_t take = std::min<std::size_t>(op.size() - 4, cap - buf.size());
            buf.insert(buf.end(), op.data() + 4, op.data() + 4 + take);
            cur = sqlfmt::readBE32(op.data());
        }
        json cols = json::array();
        const sqlfmt::RecordHeader rh = sqlfmt::parseRecordHeader(buf.data(), buf.size());
        std::uint64_t pos = rh.headerSize;
        for (const std::uint64_t st : rh.serialTypes) {
            const std::uint64_t need = sqlfmt::serialTypeSize(st);
            const std::size_t here = pos < buf.size() ? buf.size() - pos : 0;
            const sqlfmt::CellValue v = sqlfmt::decodeValue(
                st, buf.data() + std::min<std::size_t>(pos, buf.size()),
                std::min<std::size_t>(static_cast<std::size_t>(need), here));
            json cj = valueJson(st, v);
            const std::uint64_t colEnd = pos + need;
            if (need > 0 && colEnd > localBytes) {  // this column spills into overflow
                cj["fromOverflow"] = true;
                const bool isText = st >= 13 && (st % 2) == 1;
                // Break the column's bytes into per-page segments: the leaf portion
                // (page n), then each overflow page the value crosses.
                json segs = json::array();
                auto pushSeg = [&](std::int64_t pg, std::uint64_t from, std::uint64_t to) {
                    json seg = {{"page", pg}, {"bytes", to - from}};
                    if (isText && to <= buf.size()) seg["text"] = std::string(buf.begin() + from, buf.begin() + to);
                    segs.push_back(std::move(seg));
                };
                if (pos < localBytes) pushSeg(n, pos, std::min(colEnd, localBytes));
                std::uint64_t ob = std::max<std::uint64_t>(pos, localBytes);  // payload offset in overflow
                while (ob < colEnd) {
                    const std::uint64_t idx = (ob - localBytes) / chunk;
                    const std::uint64_t pageEnd = localBytes + (idx + 1) * chunk;
                    const std::uint64_t segEnd = std::min(colEnd, pageEnd);
                    pushSeg(idx < ovPages.size() ? ovPages[idx] : 0, ob, segEnd);
                    ob = segEnd;
                }
                cj["segments"] = std::move(segs);
            }
            cols.push_back(std::move(cj));
            pos += need;
        }
        return cols;
    };

    if (isBtree(pageType)) {
        const std::size_t hoff = (n == 1) ? 100 : 0;
        const sqlfmt::BtreeHeader bh = sqlfmt::parseBtreeHeader(page.data(), hoff);
        const bool interior = (bh.type == 2 || bh.type == 5);
        const bool tableBtree = (bh.type == 5 || bh.type == 13);
        const std::size_t ptrBase = hoff + bh.headerSize;

        header = {{"type", bh.type}, {"typeName", btreeTypeName(bh.type)},
                  {"cellCount", bh.cellCount},
                  {"cellContentStart", bh.cellContentStart},
                  {"firstFreeblock", bh.firstFreeblock},
                  {"fragmentedFreeBytes", bh.fragmentedFreeBytes}};
        if (interior) {
            header["rightmostPointer"] = bh.rightmostPointer;
            if (bh.rightmostPointer > 0)
                pointers.push_back({{"toPage", bh.rightmostPointer}, {"kind", "child"}});
        }

        if (n == 1) regions.push_back({{"offset", 0}, {"length", 100}, {"kind", "db-header"}});
        regions.push_back({{"offset", hoff}, {"length", bh.headerSize}, {"kind", "page-header"}});
        regions.push_back({{"offset", ptrBase}, {"length", 2 * bh.cellCount}, {"kind", "cellptr-array"}});

        for (int i = 0; i < bh.cellCount; ++i) {
            const std::size_t co = readBE16(ptrBase + 2 * static_cast<std::size_t>(i));
            if (co < 1 || co >= pageEnd) continue;
            std::size_t p = co;
            json cell = {{"cellIndex", i}, {"offset", static_cast<std::int64_t>(co)}};
            std::optional<std::int64_t> overflow;

            if (interior && tableBtree) {  // table-interior: leftChild + rowid
                const std::int64_t left = readBE32(p);
                const sqlfmt::Varint rowid = varintAt(p + 4);
                cell["leftChild"] = left;
                cell["rowid"] = static_cast<std::int64_t>(rowid.value);
                cell["size"] = 4 + rowid.length;
                if (left > 0) pointers.push_back({{"toPage", left}, {"kind", "child"}});
            } else {
                std::int64_t left = 0;
                if (interior) { left = readBE32(p); p += 4; cell["leftChild"] = left; }
                const sqlfmt::Varint payload = varintAt(p);
                std::size_t recStart = p + payload.length;
                std::size_t rowidLen = 0;
                if (tableBtree) {  // table-leaf: rowid before the record
                    const sqlfmt::Varint rowid = varintAt(recStart);
                    rowidLen = rowid.length;
                    recStart += rowid.length;
                    cell["rowid"] = static_cast<std::int64_t>(rowid.value);
                }
                const sqlfmt::PayloadSplit split =
                    sqlfmt::localPayload(payload.value, usableSize_, tableBtree);
                std::int64_t firstOverflow = 0;
                if (split.hasOverflow) {
                    firstOverflow = readBE32(recStart + split.localBytes);
                    overflow = firstOverflow;
                }
                cell["columns"] = decodeRecord(recStart, split.localBytes, payload.value, firstOverflow);
                cell["payloadBytes"] = static_cast<std::int64_t>(payload.value);
                const std::size_t size = (interior ? 4 : 0) + payload.length + rowidLen +
                                         split.localBytes + (split.hasOverflow ? 4 : 0);
                if (left > 0) pointers.push_back({{"toPage", left}, {"kind", "child"}});
                cell["size"] = static_cast<std::int64_t>(size);
            }
            if (overflow && *overflow > 0) {
                cell["overflowPage"] = *overflow;
                pointers.push_back({{"toPage", *overflow}, {"kind", "overflow"}});
            }
            regions.push_back({{"offset", static_cast<std::int64_t>(co)},
                               {"length", cell.value("size", json(0))}, {"kind", "cell"},
                               {"cellIndex", i}});
            cells.push_back(std::move(cell));
        }
    } else if (pageType == "overflow") {
        const std::int64_t next = readBE32(0);
        header["nextPage"] = next;
        regions.push_back({{"offset", 0}, {"length", 4}, {"kind", "overflow-header"}});
        regions.push_back({{"offset", 4}, {"length", static_cast<std::int64_t>(usable) - 4},
                           {"kind", "payload"}});
        if (next > 0) pointers.push_back({{"toPage", next}, {"kind", "overflow"}});
    } else if (pageType == "freelist-trunk") {
        const std::int64_t next = readBE32(0);
        const std::int64_t count = readBE32(4);
        header["nextTrunk"] = next;
        header["leafCount"] = count;
        regions.push_back({{"offset", 0}, {"length", 8}, {"kind", "freelist-header"}});
        const std::int64_t arrLen = std::min<std::int64_t>(count, (static_cast<std::int64_t>(usable) - 8) / 4) * 4;
        if (arrLen > 0) regions.push_back({{"offset", 8}, {"length", arrLen}, {"kind", "freelist-array"}});
        if (next > 0) pointers.push_back({{"toPage", next}, {"kind", "freelist-next"}});
        for (std::int64_t k = 0; k < count; ++k) {
            const std::int64_t leaf = readBE32(8 + 4 * static_cast<std::size_t>(k));
            if (leaf > 0) pointers.push_back({{"toPage", leaf}, {"kind", "freelist-leaf"}});
        }
    } else {  // freelist-leaf, pointer-map, lock-byte, unallocated, unknown
        regions.push_back({{"offset", 0}, {"length", static_cast<std::int64_t>(usable)},
                           {"kind", pageType.empty() ? "unknown" : pageType}});
    }

    // Annotate each pointer with its target's page type (from the map) so the UI
    // can show it with type symbology.
    if (pageTypeOf) {
        for (json& ptr : pointers) {
            ptr["pageType"] = pageTypeOf(ptr.value("toPage", static_cast<std::int64_t>(0)));
        }
    }

    json out = {
        {"pageNumber", n}, {"pageType", pageType},
        {"pageSize", pageSize_}, {"usableSize", usableSize_},
        {"header", std::move(header)},
        {"regions", fillGaps(regions, usableSize_, pageSize_)},
        {"cells", std::move(cells)},
        {"pointers", std::move(pointers)},
    };
    // Replace invalid UTF-8 (a value/segment can end mid code point at a page or
    // cap boundary) rather than throwing.
    return out.dump(-1, ' ', false, json::error_handler_t::replace);
}
