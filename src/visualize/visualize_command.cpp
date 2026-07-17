#include "visualize/visualize_command.hpp"

#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "visualize/embedded_assets.hpp"
#include "visualize/map_db.hpp"
#include "visualize/page_content.hpp"
#include "visualize/query_engine.hpp"

namespace {

std::string contentTypeFor(const std::string& name) {
    if (name.ends_with(".html")) return "text/html; charset=utf-8";
    if (name.ends_with(".js")) return "application/javascript; charset=utf-8";
    if (name.ends_with(".css")) return "text/css; charset=utf-8";
    return "application/octet-stream";
}

void serveAsset(httplib::Response& res, const std::string& name) {
    const EmbeddedAsset* asset = findEmbeddedAsset(name);
    if (asset == nullptr) {
        res.status = 404;
        res.set_content("not found", "text/plain");
        return;
    }
    res.set_content(asset->data, asset->size, contentTypeFor(name));
}

std::int64_t paramInt(const httplib::Request& req, const char* key,
                      std::int64_t fallback) {
    if (!req.has_param(key)) return fallback;
    try {
        return std::stoll(req.get_param_value(key));
    } catch (...) {
        return fallback;
    }
}

std::string paramStr(const httplib::Request& req, const char* key, const char* fallback) {
    return req.has_param(key) ? req.get_param_value(key) : fallback;
}

// Parses a comma-separated list of profile leaf ids (e.g. "0,2,3"). An absent
// or empty `sel` yields an empty filter, which the data layer treats as "all".
std::vector<int> paramLeaves(const httplib::Request& req) {
    std::vector<int> ids;
    if (!req.has_param("sel")) return ids;
    std::stringstream ss(req.get_param_value("sel"));
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) continue;
        try {
            ids.push_back(std::stoi(tok));
        } catch (...) {
        }
    }
    return ids;
}

}  // namespace

void configureVisualizeRoutes(httplib::Server& server, MapDb& db, QueryEngine* engine,
                              PageContent* content) {
    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        serveAsset(res, "index.html");
    });
    server.Get(R"(/static/(.+))",
               [](const httplib::Request& req, httplib::Response& res) {
                   serveAsset(res, req.matches[1].str());
               });

    server.Get("/api/meta", [&db](const httplib::Request&, httplib::Response& res) {
        res.set_content(db.metaJson(), "application/json");
    });

    server.Get("/api/pages", [&db](const httplib::Request& req,
                                   httplib::Response& res) {
        const std::int64_t from = paramInt(req, "from", 1);
        const std::int64_t to = paramInt(req, "to", from);
        bool tooLarge = false;
        std::string body = db.pagesJson(from, to, tooLarge);
        if (tooLarge) {
            res.status = 413;
            res.set_content(R"({"error":"range too large; use /api/runs"})",
                            "application/json");
            return;
        }
        res.set_content(body, "application/json");
    });

    server.Get("/api/runs", [&db](const httplib::Request& req,
                                  httplib::Response& res) {
        const std::int64_t from = paramInt(req, "from", 1);
        const std::int64_t to = paramInt(req, "to", from);
        const bool profiled = req.has_param("profiled");
        res.set_content(db.runsJson(from, to, profiled, paramLeaves(req)),
                        "application/json");
    });

    server.Get("/api/object/pages", [&db](const httplib::Request& req,
                                          httplib::Response& res) {
        const std::int64_t objectId = paramInt(req, "objectId", 0);
        const std::int64_t from = paramInt(req, "from", 0);
        const std::int64_t to = paramInt(req, "to", from);
        bool tooLarge = false;
        std::string body = db.objectPagesJson(objectId, from, to, tooLarge);
        if (tooLarge) {
            res.status = 413;
            res.set_content(R"({"error":"range too large"})", "application/json");
            return;
        }
        res.set_content(body, "application/json");
    });

    server.Get(R"(/api/page/(\d+))",
               [&db](const httplib::Request& req, httplib::Response& res) {
                   const std::int64_t n = std::stoll(req.matches[1].str());
                   std::string body = db.pageJson(n, paramLeaves(req));
                   if (body.empty()) {
                       res.status = 404;
                       res.set_content(R"({"error":"no such page"})",
                                       "application/json");
                       return;
                   }
                   res.set_content(body, "application/json");
               });

    server.Get("/api/profile/pages", [&db](const httplib::Request& req,
                                           httplib::Response& res) {
        const std::int64_t from = paramInt(req, "from", 1);
        const std::int64_t to = paramInt(req, "to", from);
        res.set_content(db.profilePagesJson(from, to, paramLeaves(req)),
                        "application/json");
    });

    // Page Tree view (b-tree structure from the map; lazy/windowed).
    server.Get("/api/tree/roots", [&db](const httplib::Request&, httplib::Response& res) {
        res.set_content(db.treeRootsJson(), "application/json");
    });
    server.Get("/api/tree/children", [&db](const httplib::Request& req, httplib::Response& res) {
        res.set_content(db.treeChildrenJson(paramInt(req, "page", 0)), "application/json");
    });
    server.Get("/api/tree/freelist", [&db](const httplib::Request& req, httplib::Response& res) {
        res.set_content(db.treeFreelistJson(paramInt(req, "after", 0), paramInt(req, "limit", 1000)),
                        "application/json");
    });
    server.Get("/api/tree/other", [&db](const httplib::Request& req, httplib::Response& res) {
        res.set_content(db.treeOtherJson(paramInt(req, "after", 0), paramInt(req, "limit", 1000)),
                        "application/json");
    });
    server.Get("/api/tree/path", [&db](const httplib::Request& req, httplib::Response& res) {
        res.set_content(db.treePathJson(paramInt(req, "page", 0)), "application/json");
    });
    server.Get("/api/tree/search", [&db](const httplib::Request& req, httplib::Response& res) {
        res.set_content(
            db.treeSearchJson(paramStr(req, "q", ""), static_cast<int>(paramInt(req, "limit", 20))),
            "application/json");
    });
    server.Get("/api/tree/object", [&db](const httplib::Request& req, httplib::Response& res) {
        const std::string body = db.treeObjectOverviewJson(paramInt(req, "id", 0));
        if (body.empty()) {
            res.status = 404;
            res.set_content(R"({"error":"no such object"})", "application/json");
            return;
        }
        res.set_content(body, "application/json");
    });

    // Page detail with decoded values (only with --db-file).
    if (content != nullptr) {
        server.Get(R"(/api/page/(\d+)/content)",
                   [&db, content](const httplib::Request& req, httplib::Response& res) {
                       const std::int64_t n = std::stoll(req.matches[1].str());
                       const std::string type = db.pageType(n);
                       auto typeOf = [&db](std::int64_t p) { return db.pageType(p); };
                       std::string body = type.empty() ? std::string() : content->pageJson(n, type, typeOf);
                       // Interpret an overflow page through its owning leaf/interior
                       // page: show the owning cell's decoded record for this page.
                       if (!body.empty() && type == "overflow") {
                           const std::int64_t owner = db.overflowOwner(n);
                           if (owner > 0) {
                               try {
                                   nlohmann::json ovf = nlohmann::json::parse(body);
                                   nlohmann::json own = nlohmann::json::parse(
                                       content->pageJson(owner, db.pageType(owner), typeOf));
                                   nlohmann::json owningCell;  // the cell whose value reaches this page
                                   for (const auto& cell : own.value("cells", nlohmann::json::array())) {
                                       bool match = false;
                                       for (const auto& col : cell.value("columns", nlohmann::json::array()))
                                           for (const auto& seg : col.value("segments", nlohmann::json::array()))
                                               if (seg.value("page", std::int64_t{0}) == n) match = true;
                                       if (match) { owningCell = cell; break; }
                                   }
                                   ovf["ownerPage"] = owner;
                                   if (!owningCell.is_null()) {
                                       // Restrict the owning cell to just the data that
                                       // physically lives on THIS overflow page: keep only
                                       // columns with a segment on page n, reduced to that
                                       // page's slice.
                                       nlohmann::json cols = nlohmann::json::array();
                                       for (const auto& col :
                                            owningCell.value("columns", nlohmann::json::array())) {
                                           nlohmann::json seg;
                                           for (const auto& s : col.value("segments", nlohmann::json::array()))
                                               if (s.value("page", std::int64_t{0}) == n) { seg = s; break; }
                                           if (seg.is_null()) continue;
                                           nlohmann::json c = col;
                                           c.erase("segments");
                                           c.erase("fromOverflow");
                                           c["bytes"] = seg.value("bytes", 0);
                                           if (col.value("type", std::string()) == "text") {
                                               c["value"] = seg.value("text", std::string());
                                               c["truncated"] = false;
                                           }
                                           cols.push_back(std::move(c));
                                       }
                                       owningCell["columns"] = std::move(cols);
                                       const std::int64_t ci = owningCell.value("cellIndex", std::int64_t{-1});
                                       ovf["cells"] = nlohmann::json::array({owningCell});
                                       for (auto& region : ovf["regions"])
                                           if (region.value("kind", std::string()) == "payload")
                                               region["cellIndex"] = ci;
                                   }
                                   body = ovf.dump(-1, ' ', false,
                                                   nlohmann::json::error_handler_t::replace);
                               } catch (...) {
                               }
                           }
                       }
                       // Table-interior pages: annotate each divider cell (and the
                       // rightmost pointer) with the actual rowids of its child
                       // subtree — a count and collapsed runs (rowids have gaps).
                       if (!body.empty() && type == "table-interior") {
                           try {
                               nlohmann::json j = nlohmann::json::parse(body);
                               nlohmann::json r = nlohmann::json::parse(db.tableInteriorRowRunsJson(n));
                               // Merge the rowid runs into the decoded cells (keeping
                               // cellIndex/offset/size), matched by the child page —
                               // the runs are keyed by `leftChild`.
                               std::map<std::int64_t, nlohmann::json> byChild;
                               for (const auto& rc : r.value("cells", nlohmann::json::array()))
                                   if (rc.value("leftChild", nlohmann::json()).is_number())
                                       byChild[rc["leftChild"].get<std::int64_t>()] = rc;
                               for (auto& cell : j["cells"]) {
                                   if (!cell.value("leftChild", nlohmann::json()).is_number()) continue;
                                   auto it = byChild.find(cell["leftChild"].get<std::int64_t>());
                                   if (it == byChild.end()) continue;
                                   for (const char* k : {"rowCount", "runCount", "rowRuns"})
                                       if (it->second.contains(k)) cell[k] = it->second[k];
                               }
                               if (r.contains("rightmost")) j["rightmostRowRuns"] = r["rightmost"];
                               body = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                           } catch (...) {
                           }
                       }
                       if (body.empty()) {
                           res.status = 404;
                           res.set_content(R"({"error":"no such page"})", "application/json");
                           return;
                       }
                       res.set_content(body, "application/json");
                   });
    }

    if (engine == nullptr) return;  // live-query routes only with --db-file

    server.Post("/api/query/run", [engine](const httplib::Request& req, httplib::Response& res) {
        const std::string body = engine->runJson(req.body);
        if (body.rfind("{\"error\"", 0) == 0) res.status = 400;  // prefix match
        res.set_content(body, "application/json");
    });

    server.Post("/api/query/explain", [engine](const httplib::Request& req, httplib::Response& res) {
        res.set_content(engine->explainJson(req.body), "application/json");
    });

    server.Get(R"(/api/query/history)", [engine](const httplib::Request&, httplib::Response& res) {
        res.set_content(engine->historyJson(), "application/json");
    });

    server.Get(R"(/api/query/history/(\d+))",
               [engine](const httplib::Request& req, httplib::Response& res) {
                   res.set_content(engine->historyEntryJson(std::stoi(req.matches[1].str())),
                                   "application/json");
               });

    server.Get(R"(/api/query/(\d+)/rows)",
               [engine](const httplib::Request& req, httplib::Response& res) {
                   const int id = std::stoi(req.matches[1].str());
                   const std::int64_t from = paramInt(req, "from", 0);
                   const std::int64_t to = paramInt(req, "to", from);
                   res.set_content(engine->rowsJson(id, from, to), "application/json");
               });
}

// Reads the map's declared page size from /api/meta so the query engine can map
// byte offsets to page numbers without an unmeasured read of the db file.
int mapPageSize(const MapDb& db) {
    try {
        auto meta = nlohmann::json::parse(db.metaJson());
        return meta.at("meta").value("pageSize", 0);
    } catch (...) {
        return 0;
    }
}

int runVisualizeServe(const VisualizeOptions& options) {
    std::optional<MapDb> db;
    std::optional<QueryEngine> engine;
    std::optional<PageContent> content;
    try {
        db.emplace(options.mapFile);
        if (!options.profileFile.empty()) {
            db->loadProfile(options.profileFile);
        }
        if (!options.dbFile.empty()) {
            engine.emplace(options.dbFile, mapPageSize(*db), *db);
            content.emplace(PageContent::open(options.dbFile));
            db->setHasDb(true);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    httplib::Server server;
    configureVisualizeRoutes(server, *db, engine ? &*engine : nullptr,
                             content ? &*content : nullptr);

    const char* host = "127.0.0.1";
    int port = options.port;
    if (port == 0) {
        port = server.bind_to_any_port(host);
        if (port < 0) {
            std::cerr << "visualize: failed to bind a port\n";
            return 1;
        }
    } else if (!server.bind_to_port(host, port)) {
        std::cerr << "visualize: port " << port << " is unavailable\n";
        return 1;
    }

    std::cout << "SQLinsite visualize serving on http://" << host << ":" << port
              << "  (Ctrl-C to stop)\n";
    server.listen_after_bind();
    return 0;
}
