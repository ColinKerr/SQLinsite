#pragma once

#include <cstdint>
#include <string>

struct sqlite3;

// Parses a Cloud Backed SQLite `manifest.bcv` (format version 4) into a temp SQLite
// database describing how the container's blocks assemble into its databases, for
// the Block and Analysis views. The temp db (attached by readers as `manifest`)
// holds:
//   meta(formatVersion, blockSize, blockIdSize, nDb, nDelete, maxDbId,
//        selectedDbId, pagesPerBlock, blockCount, manifestDbName,
//        manifestMatch, matchReason)                                -- single row
//   databases(id, parent, version, name, blockCount, entryCount, deleted, isSelected)
//   blocks(dbId, blockIndex, blockId TEXT /*hex*/, sharedWithParent)  -- resolved
//
// It also selects which named database in the manifest the mapped file corresponds
// to — an explicit name, else the non-deleted, non-BASELINE db whose block count
// matches `ceil(mapPageCount / pagesPerBlock)` — and reports whether it matches. The
// temp file (and its WAL sidecars) are deleted on destruction.
class ManifestDb {
public:
    // Bump if the manifest formats this understands change.
    static constexpr int kSupportedManifestVersion = 4;

    // Parses `manifestPath`, builds the temp db, and resolves the selected db
    // against the mapped file's page count/size. Throws std::runtime_error if the
    // manifest cannot be read or parsed.
    ManifestDb(const std::string& manifestPath, const std::string& dbName,
               std::int64_t mapPageCount, int pageSize);
    ~ManifestDb();

    ManifestDb(const ManifestDb&) = delete;
    ManifestDb& operator=(const ManifestDb&) = delete;

    // Path a reader should `ATTACH DATABASE … AS manifest`.
    const std::string& path() const { return path_; }

    std::int64_t blockSize() const { return blockSize_; }
    std::int64_t pagesPerBlock() const { return pagesPerBlock_; }
    std::int64_t blockCount() const { return blockCount_; }  // selected db; 0 if none
    std::int64_t selectedDbId() const { return selectedDbId_; }
    const std::string& selectedDbName() const { return selectedDbName_; }
    bool match() const { return match_; }
    const std::string& matchReason() const { return matchReason_; }

private:
    std::string path_;
    sqlite3* db_ = nullptr;
    std::int64_t blockSize_ = 0;
    std::int64_t pagesPerBlock_ = 0;
    std::int64_t blockCount_ = 0;
    std::int64_t selectedDbId_ = 0;
    std::string selectedDbName_;
    std::string matchReason_;
    bool match_ = false;
};
