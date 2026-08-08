#include "visualize/manifest_db.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <sqlite3.h>

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("visualize: manifest: " + what);
}

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o + 4 > b.size()) fail("truncated manifest");
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o + 1]) << 16) |
           (std::uint32_t(b[o + 2]) << 8) | std::uint32_t(b[o + 3]);
}

std::string toHex(const std::vector<std::uint8_t>& b, std::size_t o, std::size_t n) {
    static const char* k = "0123456789abcdef";
    if (o + n > b.size()) fail("truncated block id");
    std::string s;
    s.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        s.push_back(k[b[o + i] >> 4]);
        s.push_back(k[b[o + i] & 0xF]);
    }
    return s;
}

std::string makeTempPath() {
    static std::atomic<unsigned> counter{0};
    const auto dir = std::filesystem::temp_directory_path();
    const std::string name =
        "sqlinsite-manifest-" +
        std::to_string(
#if defined(_WIN32)
            static_cast<long>(_getpid())
#else
            static_cast<long>(::getpid())
#endif
            ) +
        "-" + std::to_string(counter++) + ".sqlite";
    return (dir / name).string();
}

// One per-database header from the manifest (offsets per format v4).
struct DbHdr {
    std::int64_t id = 0, parent = 0, version = 0, arrOff = 0, nBlk = 0, nEntry = 0;
    bool deleted = false;
    std::string name;
};

void exec(sqlite3* db, const std::string& sql) {
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db);
        fail(sql.substr(0, 40) + "…: " + msg);
    }
}

}  // namespace

ManifestDb::ManifestDb(const std::string& manifestPath, const std::string& dbName,
                       std::int64_t mapPageCount, int pageSize) {
    // ---- read + parse the manifest -----------------------------------------
    std::ifstream in(manifestPath, std::ios::binary);
    if (!in) fail("cannot read " + manifestPath);
    std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
    if (b.size() < 24) fail("file too small");
    const std::int64_t version = be32(b, 0);
    if (version != kSupportedManifestVersion) {
        fail("unsupported manifest version " + std::to_string(version));
    }
    const std::int64_t szBlk = be32(b, 4);
    const std::int64_t nDb = be32(b, 8);
    const std::int64_t nDelete = be32(b, 12);
    const std::int64_t nName = be32(b, 16);
    const std::int64_t maxDbId = be32(b, 20);
    if (szBlk <= 0 || pageSize <= 0) fail("bad block/page size");

    std::vector<DbHdr> dbs;
    dbs.reserve(static_cast<std::size_t>(nDb));
    for (std::int64_t i = 0; i < nDb; ++i) {
        const std::size_t o = 24 + 152 * static_cast<std::size_t>(i);
        DbHdr d;
        d.id = be32(b, o);
        d.parent = be32(b, o + 4);
        d.version = be32(b, o + 8);
        d.arrOff = be32(b, o + 12);
        const std::uint32_t nblk = be32(b, o + 16);
        d.deleted = (nblk & 0x80000000u) != 0;
        d.nBlk = nblk & 0x7FFFFFFFu;
        d.nEntry = be32(b, o + 20);
        const std::size_t ns = o + 24;
        std::size_t len = 0;
        while (len < 128 && ns + len < b.size() && b[ns + len] != 0) ++len;
        d.name.assign(b.begin() + ns, b.begin() + ns + len);
        dbs.push_back(std::move(d));
    }

    auto byId = [&](std::int64_t id) -> const DbHdr* {
        for (const DbHdr& d : dbs)
            if (d.id == id) return &d;
        return nullptr;
    };

    // Fully-resolved ordered block-id arrays (hex), memoized; children apply their
    // (blockIndex, blockId) deltas onto the parent's array.
    std::map<std::int64_t, std::vector<std::string>> arrays;
    std::function<const std::vector<std::string>&(std::int64_t)> resolve =
        [&](std::int64_t id) -> const std::vector<std::string>& {
        auto it = arrays.find(id);
        if (it != arrays.end()) return it->second;
        const DbHdr* d = byId(id);
        std::vector<std::string> arr;
        if (d == nullptr) return arrays.emplace(id, std::move(arr)).first->second;
        if (d->parent == 0) {
            arr.reserve(static_cast<std::size_t>(d->nBlk));
            for (std::int64_t k = 0; k < d->nBlk; ++k)
                arr.push_back(toHex(b, static_cast<std::size_t>(d->arrOff + k * nName),
                                    static_cast<std::size_t>(nName)));
        } else {
            arr = resolve(d->parent);  // copy parent's full array
            arr.resize(static_cast<std::size_t>(d->nBlk));
            const std::int64_t stride = 4 + nName;
            for (std::int64_t k = 0; k < d->nEntry; ++k) {
                const std::size_t p = static_cast<std::size_t>(d->arrOff + k * stride);
                const std::int64_t idx = be32(b, p);
                if (idx >= 0 && idx < d->nBlk)
                    arr[static_cast<std::size_t>(idx)] =
                        toHex(b, p + 4, static_cast<std::size_t>(nName));
            }
        }
        return arrays.emplace(id, std::move(arr)).first->second;
    };

    // ---- select the database matching the mapped file ----------------------
    pagesPerBlock_ = szBlk / pageSize;
    blockSize_ = szBlk;
    const std::int64_t expected =
        pagesPerBlock_ > 0 ? (mapPageCount + pagesPerBlock_ - 1) / pagesPerBlock_ : 0;

    const DbHdr* selected = nullptr;
    if (!dbName.empty()) {
        selected = byId(-1);  // placeholder
        for (const DbHdr& d : dbs)
            if (!d.deleted && d.name == dbName) selected = &d;
        if (selected == nullptr)
            matchReason_ = "named database '" + dbName + "' not found in manifest";
    } else {
        // Auto: prefer a non-deleted, non-BASELINE db whose block count matches.
        const DbHdr* best = nullptr;   // best matching by block count
        const DbHdr* newest = nullptr; // fallback: newest non-baseline
        for (const DbHdr& d : dbs) {
            if (d.deleted || d.name == "BASELINE.bim") continue;
            if (newest == nullptr || d.id > newest->id) newest = &d;
            if (d.nBlk == expected && (best == nullptr || d.id > best->id)) best = &d;
        }
        selected = best ? best : newest;
        if (best == nullptr)
            matchReason_ = "no manifest database's block count matches the map (expected " +
                           std::to_string(expected) + " blocks)";
    }
    if (selected != nullptr) {
        selectedDbId_ = selected->id;
        selectedDbName_ = selected->name;
        blockCount_ = selected->nBlk;
        match_ = (selected->nBlk == expected);
        if (!match_ && matchReason_.empty())
            matchReason_ = "selected database '" + selected->name + "' has " +
                           std::to_string(selected->nBlk) + " blocks; the map expects " +
                           std::to_string(expected);
    }

    // ---- build the temp SQLite db ------------------------------------------
    path_ = makeTempPath();
    std::remove(path_.c_str());
    if (sqlite3_open_v2(path_.c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        fail("cannot create temp db");
    }
    exec(db_, "PRAGMA journal_mode=WAL; PRAGMA synchronous=OFF;");
    exec(db_,
         "CREATE TABLE meta(formatVersion,blockSize,blockIdSize,nDb,nDelete,maxDbId,"
         "selectedDbId,pagesPerBlock,blockCount,manifestDbName,manifestMatch,matchReason);"
         "CREATE TABLE databases(id INTEGER PRIMARY KEY,parent,version,name,"
         "blockCount,entryCount,deleted,isSelected);"
         "CREATE TABLE blocks(dbId INTEGER,blockIndex INTEGER,blockId TEXT,"
         "sharedWithParent INTEGER,PRIMARY KEY(dbId,blockIndex));");
    exec(db_, "BEGIN");

    sqlite3_stmt* m = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO meta VALUES(?,?,?,?,?,?,?,?,?,?,?,?)", -1, &m, nullptr);
    sqlite3_bind_int64(m, 1, version);
    sqlite3_bind_int64(m, 2, szBlk);
    sqlite3_bind_int64(m, 3, nName);
    sqlite3_bind_int64(m, 4, nDb);
    sqlite3_bind_int64(m, 5, nDelete);
    sqlite3_bind_int64(m, 6, maxDbId);
    sqlite3_bind_int64(m, 7, selectedDbId_);
    sqlite3_bind_int64(m, 8, pagesPerBlock_);
    sqlite3_bind_int64(m, 9, blockCount_);
    sqlite3_bind_text(m, 10, selectedDbName_.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(m, 11, match_ ? 1 : 0);
    sqlite3_bind_text(m, 12, matchReason_.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(m);
    sqlite3_finalize(m);

    sqlite3_stmt* ds = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO databases VALUES(?,?,?,?,?,?,?,?)", -1, &ds, nullptr);
    sqlite3_stmt* bs = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO blocks VALUES(?,?,?,?)", -1, &bs, nullptr);
    for (const DbHdr& d : dbs) {
        sqlite3_bind_int64(ds, 1, d.id);
        sqlite3_bind_int64(ds, 2, d.parent);
        sqlite3_bind_int64(ds, 3, d.version);
        sqlite3_bind_text(ds, 4, d.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(ds, 5, d.nBlk);
        sqlite3_bind_int64(ds, 6, d.nEntry);
        sqlite3_bind_int(ds, 7, d.deleted ? 1 : 0);
        sqlite3_bind_int(ds, 8, d.id == selectedDbId_ ? 1 : 0);
        sqlite3_step(ds);
        sqlite3_reset(ds);
        if (d.deleted) continue;  // only resolve/store live databases

        const std::vector<std::string>& arr = resolve(d.id);
        const std::vector<std::string>* parr =
            d.parent ? &resolve(d.parent) : nullptr;
        for (std::size_t i = 0; i < arr.size(); ++i) {
            const bool shared = parr && i < parr->size() && (*parr)[i] == arr[i];
            sqlite3_bind_int64(bs, 1, d.id);
            sqlite3_bind_int64(bs, 2, static_cast<std::int64_t>(i));
            sqlite3_bind_text(bs, 3, arr[i].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(bs, 4, shared ? 1 : 0);
            sqlite3_step(bs);
            sqlite3_reset(bs);
        }
    }
    sqlite3_finalize(ds);
    sqlite3_finalize(bs);
    exec(db_, "COMMIT");
    exec(db_, "CREATE INDEX blocks_db ON blocks(dbId)");
}

ManifestDb::~ManifestDb() {
    if (db_) sqlite3_close(db_);
    std::remove(path_.c_str());
    std::remove((path_ + "-wal").c_str());
    std::remove((path_ + "-shm").c_str());
}
