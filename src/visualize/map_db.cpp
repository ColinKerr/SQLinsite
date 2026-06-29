#include "visualize/map_db.hpp"

#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "visualize/profile_reader.hpp"

using nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("visualize: " + what);
}

// Reads a column as a JSON value (null-aware).
json columnJson(sqlite3_stmt* s, int col) {
    switch (sqlite3_column_type(s, col)) {
        case SQLITE_NULL: return nullptr;
        case SQLITE_INTEGER: return sqlite3_column_int64(s, col);
        case SQLITE_FLOAT: return sqlite3_column_double(s, col);
        default: {
            const unsigned char* t = sqlite3_column_text(s, col);
            return t ? json(reinterpret_cast<const char*>(t)) : json(nullptr);
        }
    }
}

// Runs a query and returns an array of row objects keyed by column name.
json queryRows(sqlite3* db, const std::string& sql,
               const std::vector<std::int64_t>& binds = {}) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        fail(std::string("query failed: ") + sqlite3_errmsg(db));
    }
    for (std::size_t i = 0; i < binds.size(); ++i) {
        sqlite3_bind_int64(stmt, static_cast<int>(i + 1), binds[i]);
    }
    json rows = json::array();
    const int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        json row = json::object();
        for (int c = 0; c < cols; ++c) {
            row[sqlite3_column_name(stmt, c)] = columnJson(stmt, c);
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return rows;
}

bool tableExists(sqlite3* db, const char* name) {
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db,
                       "SELECT 1 FROM sqlite_schema WHERE type='table' AND name=?",
                       -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

}  // namespace

MapDb::MapDb(const std::string& mapPath) {
    if (sqlite3_open_v2(mapPath.c_str(), &db_, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK) {
        const std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        fail("cannot open map file: " + msg);
    }
    for (const char* t : {"meta", "objects", "pages", "runs", "type_counts"}) {
        if (!tableExists(db_, t)) {
            sqlite3_close(db_);
            db_ = nullptr;
            fail("not a sqlinsite map (missing table '" + std::string(t) + "')");
        }
    }
}

MapDb::~MapDb() { sqlite3_close(db_); }

void MapDb::loadProfile(const std::string& csvPath) {
    const ProfileAggregate agg = aggregateProfileFile(csvPath);
    sqlite3_exec(db_,
                 "CREATE TEMP TABLE profile(pageNumber INTEGER PRIMARY KEY, "
                 "reads INTEGER, writes INTEGER)",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* ins = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO profile VALUES (?,?,?)", -1, &ins,
                       nullptr);
    for (const PageAccess& a : agg.pages) {
        sqlite3_bind_int64(ins, 1, a.pageNumber);
        sqlite3_bind_int64(ins, 2, a.reads);
        sqlite3_bind_int64(ins, 3, a.writes);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }
    sqlite3_finalize(ins);
    sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
    hasProfile_ = true;
}

std::string MapDb::metaJson() const {
    json meta = json::object();
    json rows = queryRows(db_, "SELECT * FROM meta LIMIT 1");
    if (!rows.empty()) meta = rows[0];
    json j = {
        {"meta", meta},
        {"objects",
         queryRows(db_, "SELECT id,type,name,rootPage,pageCount FROM objects "
                        "ORDER BY id")},
        {"typeCounts",
         queryRows(db_, "SELECT pageType,count FROM type_counts ORDER BY pageType")},
        {"hasProfile", hasProfile_},
    };
    return j.dump();
}

std::string MapDb::pagesJson(std::int64_t from, std::int64_t to,
                             bool& tooLarge) const {
    tooLarge = (to - from + 1) > kPageRangeCap;
    if (tooLarge) return {};
    json j = {{"pages", queryRows(db_,
                                  "SELECT pageNumber,pageType,objectId FROM pages "
                                  "WHERE pageNumber BETWEEN ? AND ? ORDER BY pageNumber",
                                  {from, to})}};
    return j.dump();
}

std::string MapDb::runsJson(std::int64_t from, std::int64_t to) const {
    json j = {{"runs", queryRows(db_,
                                 "SELECT startPage,endPage,pageType,objectId FROM runs "
                                 "WHERE startPage <= ? AND endPage >= ? ORDER BY startPage",
                                 {to, from})}};
    return j.dump();
}

std::string MapDb::objectPagesJson(std::int64_t objectId, std::int64_t from,
                                   std::int64_t to, bool& tooLarge) const {
    tooLarge = (to - from + 1) > kPageRangeCap;
    if (tooLarge) return {};
    const std::int64_t limit = to - from + 1;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_,
                       "SELECT pageNumber,pageType FROM pages WHERE objectId=? "
                       "ORDER BY pageNumber LIMIT ? OFFSET ?",
                       -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, objectId);
    sqlite3_bind_int64(stmt, 2, limit);
    sqlite3_bind_int64(stmt, 3, from);
    json pages = json::array();
    std::int64_t ordinal = from;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        pages.push_back({{"ordinal", ordinal++},
                         {"pageNumber", sqlite3_column_int64(stmt, 0)},
                         {"pageType",
                          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))}});
    }
    sqlite3_finalize(stmt);
    return json({{"pages", std::move(pages)}}).dump();
}

std::string MapDb::pageJson(std::int64_t pageNumber) const {
    json rows = queryRows(db_, "SELECT * FROM pages WHERE pageNumber=?",
                          {pageNumber});
    if (rows.empty()) return {};
    json j = rows[0];
    j["pointers"] = queryRows(db_,
                              "SELECT toPage,kind FROM pointers WHERE fromPage=?",
                              {pageNumber});
    j["cells"] = queryRows(db_,
                           "SELECT cellIndex,rowid,leftChild,payloadBytes,localBytes,"
                           "overflowPage,keyJson FROM cells WHERE pageNumber=? "
                           "ORDER BY cellIndex",
                           {pageNumber});
    j["ptrmap"] = queryRows(db_,
                            "SELECT targetPage,entryType,parentPage FROM ptrmap "
                            "WHERE pageNumber=? ORDER BY targetPage",
                            {pageNumber});
    if (hasProfile_) {
        json p = queryRows(db_, "SELECT reads,writes FROM profile WHERE pageNumber=?",
                           {pageNumber});
        j["profile"] = p.empty() ? json({{"reads", 0}, {"writes", 0}}) : p[0];
    }
    return j.dump();
}

std::string MapDb::profilePagesJson(std::int64_t from, std::int64_t to) const {
    if (!hasProfile_) return R"({"pages":[]})";
    json j = {{"pages", queryRows(db_,
                                  "SELECT pageNumber,reads,writes FROM profile "
                                  "WHERE pageNumber BETWEEN ? AND ? ORDER BY pageNumber",
                                  {from, to})}};
    return j.dump();
}

std::string MapDb::profileHistogramJson(std::int64_t from, std::int64_t to,
                                        int bins) const {
    if (bins < 1) bins = 1;
    const std::int64_t span = to - from + 1;
    const std::int64_t width = (span + bins - 1) / bins;  // ceil
    std::vector<std::int64_t> reads(bins, 0), writes(bins, 0);

    if (hasProfile_ && width > 0) {
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_,
                           "SELECT pageNumber,reads,writes FROM profile "
                           "WHERE pageNumber BETWEEN ? AND ?",
                           -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, from);
        sqlite3_bind_int64(stmt, 2, to);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const std::int64_t pg = sqlite3_column_int64(stmt, 0);
            int bin = static_cast<int>((pg - from) / width);
            if (bin < 0) bin = 0;
            if (bin >= bins) bin = bins - 1;
            reads[bin] += sqlite3_column_int64(stmt, 1);
            writes[bin] += sqlite3_column_int64(stmt, 2);
        }
        sqlite3_finalize(stmt);
    }

    json arr = json::array();
    for (int b = 0; b < bins; ++b) {
        arr.push_back({{"from", from + static_cast<std::int64_t>(b) * width},
                       {"to", from + static_cast<std::int64_t>(b + 1) * width - 1},
                       {"reads", reads[b]},
                       {"writes", writes[b]}});
    }
    return json({{"bins", arr}, {"width", width}}).dump();
}
