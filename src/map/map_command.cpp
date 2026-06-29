#include "map/map_command.hpp"

#include <iostream>

#include "map/map_builder.hpp"

int runMap(const MapOptions& options) {
    try {
        writeMap(options.testFile, options.outFile);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
