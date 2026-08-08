#pragma once

#include <cstdint>
#include <mutex>
#include <string>

struct sqlite3;

// The Analysis view's "unified analysis connection": one plain, read-only SQLite
// connection whose base (no-prefix) database is the mapped source db (`--db-file`,
// or an empty in-memory db when absent), with the map, profile, and manifest temp
// dbs attached as `map`, `profile`, and `manifest`. No profiling VFS and no history
// side effects — it answers meta/analytics queries over the map/profile/manifest and
// the primary db. `PRAGMA query_only` makes the whole connection read-only.
class AnalysisDb {
public:
    // Rows returned by one analysis query are capped at this many.
    static constexpr std::int64_t kRowCap = 100000;

    // Empty paths are simply not attached (map is always present). `dbFile` empty →
    // the base is an in-memory db and only prefixed queries work.
    AnalysisDb(const std::string& dbFile, const std::string& mapPath,
               const std::string& profilePath, const std::string& manifestPath);
    ~AnalysisDb();
    AnalysisDb(const AnalysisDb&) = delete;
    AnalysisDb& operator=(const AnalysisDb&) = delete;

    // Runs `sql`, returning {columns:[{name}], rows:[[value,...]], rowCount, truncated}
    // or {error}. Serialized (one connection); read-only.
    std::string queryJson(const std::string& sql) const;

private:
    sqlite3* db_ = nullptr;
    mutable std::mutex mu_;
};
