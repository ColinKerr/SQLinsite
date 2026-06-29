#pragma once

#include <string>

#include "map/map_model.hpp"  // for pageTypeName

// Parses the SQLite database at `sourcePath` and writes its structural map to
// `outPath` as an indexed SQLite database (see commands/map.schema.sql). Streams
// page-by-page rather than materializing the whole map. Throws
// std::runtime_error on failure.
void writeMap(const std::string& sourcePath, const std::string& outPath);
