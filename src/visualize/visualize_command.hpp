#pragma once

#include <string>

namespace httplib {
class Server;
}
class MapDb;
class QueryEngine;
class PageContent;

struct VisualizeOptions {
    std::string mapFile;
    std::string profileFile;  // optional
    std::string dbFile;       // optional — enables the live Query view
    int port = 8080;          // 0 picks a free port
};

// Registers the visualize routes (static assets + the map query API) on the
// given server, backed by `db`. When `engine` is non-null the live-query routes
// (/api/query/*) are registered too; when `content` is non-null the
// Page Tree detail route (/api/page/:n/content) is registered. Exposed for tests.
void configureVisualizeRoutes(httplib::Server& server, MapDb& db,
                              QueryEngine* engine = nullptr,
                              PageContent* content = nullptr);

// Opens the map (+ optional profile), starts the web server, and serves until
// interrupted. Returns a process exit code.
int runVisualizeServe(const VisualizeOptions& options);
