#pragma once

#include <string>

namespace httplib {
class Server;
}
class MapDb;

struct VisualizeOptions {
    std::string mapFile;
    std::string profileFile;  // optional
    int port = 8080;          // 0 picks a free port
};

// Registers the visualize routes (static assets + the map query API) on the
// given server, backed by `db`. Exposed for tests.
void configureVisualizeRoutes(httplib::Server& server, MapDb& db);

// Opens the map (+ optional profile), starts the web server, and serves until
// interrupted. Returns a process exit code.
int runVisualizeServe(const VisualizeOptions& options);
