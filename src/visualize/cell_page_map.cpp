#include "visualize/cell_page_map.hpp"

#include <algorithm>
#include <stdexcept>

#include <sqlite3.h>

#include "map/sqlite_format.hpp"

using sqlfmt::Byte;

CellPageMap CellPageMap::open(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        const std::string msg = db ? sqlite3_errmsg(db) : "cannot open database";
        sqlite3_close(db);
        throw std::runtime_error("cell_page_map: " + msg);
    }
    // A retained point-lookup over the raw pages: only the pages backing the
    // displayed rows are ever read, so this never loads the whole file.
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT data FROM sqlite_dbpage('main') WHERE pgno=?1", -1,
                           &stmt, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw std::runtime_error("cell_page_map: " + msg);
    }

    CellPageMap m;
    m.db_ = db;
    m.pageStmt_ = stmt;

    // Resolve page size / reserved bytes from the 100-byte file header on page 1.
    const std::vector<Byte> page1 = m.readPage(1);
    if (page1.size() < 100) {
        throw std::runtime_error("cell_page_map: not a SQLite database (no page 1)");
    }
    const std::uint32_t storedPageSize = sqlfmt::readBE16(page1.data() + 16);
    m.pageSize_ = storedPageSize == 1 ? 65536 : static_cast<int>(storedPageSize);
    m.usableSize_ = m.pageSize_ - page1[20];  // reserved-bytes-per-page at offset 20
    return m;
}

void CellPageMap::close() noexcept {
    if (pageStmt_ != nullptr) sqlite3_finalize(pageStmt_);
    if (db_ != nullptr) sqlite3_close(db_);
    pageStmt_ = nullptr;
    db_ = nullptr;
}

CellPageMap::~CellPageMap() { close(); }

CellPageMap::CellPageMap(CellPageMap&& other) noexcept
    : db_(other.db_), pageStmt_(other.pageStmt_), pageSize_(other.pageSize_),
      usableSize_(other.usableSize_), cache_(std::move(other.cache_)) {
    other.db_ = nullptr;
    other.pageStmt_ = nullptr;
}

CellPageMap& CellPageMap::operator=(CellPageMap&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        pageStmt_ = other.pageStmt_;
        pageSize_ = other.pageSize_;
        usableSize_ = other.usableSize_;
        cache_ = std::move(other.cache_);
        other.db_ = nullptr;
        other.pageStmt_ = nullptr;
    }
    return *this;
}

std::vector<std::uint8_t> CellPageMap::readPage(std::int64_t pageNumber) const {
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

const CellPageMap::Layout& CellPageMap::layoutFor(std::int64_t leafPage,
                                                  std::int64_t rowid) {
    const auto key = std::make_pair(leafPage, rowid);
    auto it = cache_.find(key);
    if (it == cache_.end()) {
        it = cache_.emplace(key, parseLayout(leafPage, rowid)).first;
    }
    return it->second;
}

CellPageMap::Layout CellPageMap::parseLayout(std::int64_t leafPage,
                                             std::int64_t rowid) const {
    Layout lay;
    if (pageSize_ <= 0 || usableSize_ <= 4) return lay;

    const std::vector<Byte> page = readPage(leafPage);
    if (page.size() < 8) return lay;

    const std::size_t headerOffset = (leafPage == 1) ? 100 : 0;
    if (headerOffset + 8 > page.size()) return lay;
    const sqlfmt::BtreeHeader bh = sqlfmt::parseBtreeHeader(page.data(), headerOffset);
    if (bh.type != 13) return lay;  // not a table-leaf page

    // Locate the cell whose rowid key matches by scanning the cell pointer array.
    const std::size_t ptrBase = headerOffset + bh.headerSize;
    std::int64_t cellOff = -1;
    int payloadLen = 0, rowidLen = 0;
    std::uint64_t payloadBytes = 0;
    for (int i = 0; i < bh.cellCount; ++i) {
        const std::size_t ptrOffset = ptrBase + 2 * static_cast<std::size_t>(i);
        if (ptrOffset + 2 > page.size()) return lay;
        const std::size_t off = sqlfmt::readBE16(page.data() + ptrOffset);
        if (off + 1 >= page.size()) continue;
        const std::size_t remaining = page.size() - off;
        const sqlfmt::Varint payload = sqlfmt::readVarint(page.data() + off, remaining);
        const sqlfmt::Varint rid =
            sqlfmt::readVarint(page.data() + off + payload.length, remaining - payload.length);
        if (static_cast<std::int64_t>(rid.value) == rowid) {
            cellOff = static_cast<std::int64_t>(off);
            payloadLen = payload.length;
            rowidLen = rid.length;
            payloadBytes = payload.value;
            break;
        }
    }
    if (cellOff < 0) return lay;

    const std::size_t payloadStart =
        static_cast<std::size_t>(cellOff) + payloadLen + rowidLen;
    const sqlfmt::PayloadSplit split =
        sqlfmt::localPayload(payloadBytes, usableSize_, /*tableLeaf=*/true);
    const std::uint64_t local = split.localBytes;
    const std::uint64_t chunk = static_cast<std::uint64_t>(usableSize_) - 4;

    // Walk the overflow chain (pages holding payload bytes [local, P)).
    std::vector<std::int64_t> overflow;
    if (split.hasOverflow) {
        const std::size_t ovPtrOffset = payloadStart + local;
        if (ovPtrOffset + 4 > page.size()) return lay;
        const std::uint64_t expected = (payloadBytes - local + chunk - 1) / chunk;
        std::int64_t cur = sqlfmt::readBE32(page.data() + ovPtrOffset);
        for (std::uint64_t k = 0; k < expected && cur != 0; ++k) {
            overflow.push_back(cur);
            const std::vector<Byte> op = readPage(cur);
            cur = op.size() >= 4 ? sqlfmt::readBE32(op.data()) : 0;
        }
    }

    // Assemble the record header bytes (may span local + overflow) to read the
    // per-column serial types.
    const sqlfmt::Varint hs =
        sqlfmt::readVarint(page.data() + payloadStart, page.size() - payloadStart);
    const std::uint64_t headerSize = hs.value;
    if (headerSize == 0) return lay;

    std::vector<Byte> hdr;
    hdr.reserve(headerSize);
    const std::size_t localAvail =
        std::min<std::size_t>(static_cast<std::size_t>(local), page.size() - payloadStart);
    const std::size_t localTake = std::min<std::size_t>(headerSize, localAvail);
    hdr.insert(hdr.end(), page.data() + payloadStart, page.data() + payloadStart + localTake);
    for (std::size_t oi = 0; oi < overflow.size() && hdr.size() < headerSize; ++oi) {
        const std::vector<Byte> op = readPage(overflow[oi]);
        if (op.size() <= 4) break;
        const std::size_t avail = std::min<std::size_t>(op.size() - 4, headerSize - hdr.size());
        hdr.insert(hdr.end(), op.data() + 4, op.data() + 4 + avail);
    }
    if (hdr.size() < headerSize) return lay;  // header incomplete → unresolved

    const sqlfmt::RecordHeader rh = sqlfmt::parseRecordHeader(hdr.data(), hdr.size());
    if (rh.headerSize != headerSize || rh.serialTypes.empty()) return lay;

    // Body offsets: column k occupies [colStart[k], colStart[k+1]) in the payload.
    lay.colStart.reserve(rh.serialTypes.size() + 1);
    std::uint64_t off = rh.headerSize;
    lay.colStart.push_back(off);
    for (const std::uint64_t st : rh.serialTypes) {
        off += sqlfmt::serialTypeSize(st);
        lay.colStart.push_back(off);
    }
    // The concatenated header + bodies must exactly fill the payload; if not, our
    // decode is untrustworthy, so treat the whole record as unresolved.
    if (off != payloadBytes) return lay;

    lay.ok = true;
    lay.payloadBytes = payloadBytes;
    lay.localBytes = local;
    lay.overflow = std::move(overflow);
    return lay;
}

std::vector<std::int64_t> CellPageMap::pagesForCell(std::int64_t leafPage,
                                                    std::int64_t rowid, int cid) {
    const Layout& lay = layoutFor(leafPage, rowid);
    if (!lay.ok) return {};
    const int nCols = static_cast<int>(lay.colStart.size()) - 1;
    if (cid < 0 || cid >= nCols) return {};

    const std::uint64_t L = lay.localBytes;
    const std::uint64_t P = lay.payloadBytes;
    const std::uint64_t chunk = static_cast<std::uint64_t>(usableSize_) - 4;

    // Byte window [lo, hi) of this column within the payload. Zero-length values
    // collapse to the single byte position where they sit.
    std::uint64_t lo = lay.colStart[cid];
    std::uint64_t hi = lay.colStart[cid + 1];
    if (hi <= lo) {
        lo = (P > 0 && lo >= P) ? P - 1 : lo;
        hi = lo + 1;
    }

    std::vector<std::int64_t> pages;
    if (lo < L) pages.push_back(leafPage);
    const std::uint64_t start = std::max<std::uint64_t>(lo, L);
    if (start < hi && chunk > 0) {
        const std::uint64_t idxFrom = (start - L) / chunk;
        const std::uint64_t idxTo = (hi - 1 - L) / chunk;
        for (std::uint64_t idx = idxFrom; idx <= idxTo; ++idx) {
            if (idx < lay.overflow.size()) pages.push_back(lay.overflow[idx]);
        }
    }
    return pages;
}
