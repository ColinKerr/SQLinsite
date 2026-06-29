#pragma once

#include <string>
#include <vector>

#include "map/map_command.hpp"
#include "profile/profile_command.hpp"
#include "visualize/visualize_command.hpp"

struct ParsedCli {
    enum class Kind { Profile, Map, VisualizeServe, Help, Error };

    Kind kind = Kind::Error;
    ProfileOptions profile;      // populated when kind == Profile
    MapOptions map;              // populated when kind == Map
    VisualizeOptions visualize;  // populated when kind == VisualizeServe
    std::string message;         // usage text (Help) or error text (Error)
    int exitCode = 0;            // suggested process exit code
};

// Parses CLI arguments (excluding argv[0]). Pure: performs no I/O.
ParsedCli parseCli(const std::vector<std::string>& args);

// The usage banner, exposed for tests and --help.
std::string usageText();
