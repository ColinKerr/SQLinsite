#pragma once

#include <string>

struct MapOptions {
    std::string testFile;
    std::string outFile;
};

// Parses the SQLite database structure and writes the map JSON.
// Returns a process exit code (0 on success). Errors go to stderr.
int runMap(const MapOptions& options);
