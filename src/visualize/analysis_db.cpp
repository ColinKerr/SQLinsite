#include "visualize/analysis_db.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

using nlohmann::json;

namespace {
void attachRo(sqlite3* db, const std::string& path, const char* alias) {
    if (path.empty()) return;
    char* sql = sqlite3_mprintf("ATTACH DATABASE %Q AS %s", path.c_str(), alias);
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    sqlite3_free(sql);
}
}  // namespace

AnalysisDb::AnalysisDb(const std::string& dbFile, const std::string& mapPath,
                       const std::string& profilePath, const std::string& manifestPath) {
    // Base = the primary db (read-only) or an anonymous in-memory db.
    const bool haveDb = !dbFile.empty();
    const int flags = haveDb ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (sqlite3_open_v2(haveDb ? dbFile.c_str() : ":memory:", &db_, flags, nullptr) != SQLITE_OK) {
        const std::string msg = db_ ? sqlite3_errmsg(db_) : "?";
        sqlite3_close(db_);
        throw std::runtime_error("visualize: analysis db: cannot open base: " + msg);
    }
    attachRo(db_, mapPath, "map");
    attachRo(db_, profilePath, "profile");
    attachRo(db_, manifestPath, "manifest");
    // Read-only for the whole connection (base + all attachments).
    sqlite3_exec(db_, "PRAGMA query_only=ON", nullptr, nullptr, nullptr);
}

AnalysisDb::~AnalysisDb() {
    if (db_) sqlite3_close(db_);
}

std::string AnalysisDb::queryJson(const std::string& sql) const {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return json({{"error", sqlite3_errmsg(db_)}}).dump();
    }
    const int ncol = sqlite3_column_count(stmt);
    json columns = json::array();
    for (int c = 0; c < ncol; ++c) {
        const char* nm = sqlite3_column_name(stmt, c);
        columns.push_back({{"name", nm ? nm : ""}});
    }
    json rows = json::array();
    std::int64_t rowCount = 0;
    bool truncated = false;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (rowCount >= kRowCap) { truncated = true; break; }
        json row = json::array();
        for (int c = 0; c < ncol; ++c) {
            switch (sqlite3_column_type(stmt, c)) {
                case SQLITE_INTEGER: row.push_back(sqlite3_column_int64(stmt, c)); break;
                case SQLITE_FLOAT: row.push_back(sqlite3_column_double(stmt, c)); break;
                case SQLITE_NULL: row.push_back(nullptr); break;
                case SQLITE_BLOB:
                    row.push_back("⟨blob " + std::to_string(sqlite3_column_bytes(stmt, c)) + "⟩");
                    break;
                default: {
                    const auto* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, c));
                    row.push_back(t ? std::string(t) : std::string());
                }
            }
        }
        rows.push_back(std::move(row));
        ++rowCount;
    }
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        const std::string err = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return json({{"error", err}}).dump();
    }
    sqlite3_finalize(stmt);
    return json({{"columns", std::move(columns)}, {"rows", std::move(rows)},
                 {"rowCount", rowCount}, {"truncated", truncated}}).dump();
}
