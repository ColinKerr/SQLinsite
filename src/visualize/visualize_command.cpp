#include "visualize/visualize_command.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <httplib.h>

#include "visualize/embedded_assets.hpp"
#include "visualize/map_db.hpp"

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

void configureVisualizeRoutes(httplib::Server& server, MapDb& db) {
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
}

int runVisualizeServe(const VisualizeOptions& options) {
    std::optional<MapDb> db;
    try {
        db.emplace(options.mapFile);
        if (!options.profileFile.empty()) {
            db->loadProfile(options.profileFile);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }

    httplib::Server server;
    configureVisualizeRoutes(server, *db);

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
