#include <iostream>
#include <vector>

#include "common/cli.hpp"
#include "map/map_command.hpp"
#include "profile/profile_command.hpp"
#include "visualize/visualize_command.hpp"

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    const ParsedCli parsed = parseCli(args);

    switch (parsed.kind) {
        case ParsedCli::Kind::Help:
            std::cout << parsed.message;
            return 0;
        case ParsedCli::Kind::Error:
            std::cerr << parsed.message;
            return parsed.exitCode;
        case ParsedCli::Kind::Profile:
            return runProfile(parsed.profile);
        case ParsedCli::Kind::Map:
            return runMap(parsed.map);
        case ParsedCli::Kind::VisualizeServe:
            return runVisualizeServe(parsed.visualize);
    }
    return 1;
}
