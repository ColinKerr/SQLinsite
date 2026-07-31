#include "common/cli.hpp"

#include <optional>

namespace {

bool isHelpFlag(const std::string& arg) {
    return arg == "--help" || arg == "-h" || arg == "help";
}

ParsedCli help() {
    return {.kind = ParsedCli::Kind::Help, .message = usageText(), .exitCode = 0};
}

ParsedCli error(const std::string& message) {
    return {.kind = ParsedCli::Kind::Error,
            .message = message + "\n\n" + usageText(),
            .exitCode = 2};
}

// Splits a token into flag and optional inline value (`--flag=value`).
struct Flag {
    std::string name;
    std::optional<std::string> inlineValue;
};

Flag splitFlag(const std::string& token) {
    const auto eq = token.find('=');
    if (eq == std::string::npos) {
        return {token, std::nullopt};
    }
    return {token.substr(0, eq), token.substr(eq + 1)};
}

ParsedCli parseProfile(const std::vector<std::string>& args, std::size_t start) {
    ProfileOptions options;

    for (std::size_t i = start; i < args.size(); ++i) {
        if (isHelpFlag(args[i])) {
            return help();
        }

        const Flag flag = splitFlag(args[i]);

        // Boolean flag: takes no value.
        if (flag.name == "--quiet") {
            if (flag.inlineValue) {
                return error("--quiet takes no value");
            }
            options.quiet = true;
            continue;
        }

        std::string* target = nullptr;
        std::string timing;
        if (flag.name == "--test-file") {
            target = &options.testFile;
        } else if (flag.name == "--statements") {
            target = &options.statementsFile;
        } else if (flag.name == "--out-file") {
            target = &options.outFile;
        } else if (flag.name == "--timing") {
            target = &timing;
        } else {
            return error("unknown option: " + args[i]);
        }

        if (flag.inlineValue) {
            *target = *flag.inlineValue;
        } else if (i + 1 < args.size()) {
            *target = args[++i];
        } else {
            return error("missing value for " + flag.name);
        }

        if (flag.name == "--timing") {
            if (timing == "relative") {
                options.relativeTiming = true;
            } else if (timing == "raw") {
                options.relativeTiming = false;
            } else {
                return error("--timing must be 'raw' or 'relative'");
            }
        }
    }

    std::string missing;
    if (options.testFile.empty()) missing += " --test-file";
    if (options.statementsFile.empty()) missing += " --statements";
    if (options.outFile.empty()) missing += " --out-file";
    if (!missing.empty()) {
        return error("missing required option(s):" + missing);
    }

    return {.kind = ParsedCli::Kind::Profile, .profile = options};
}

ParsedCli parseMap(const std::vector<std::string>& args, std::size_t start) {
    MapOptions options;

    for (std::size_t i = start; i < args.size(); ++i) {
        if (isHelpFlag(args[i])) {
            return help();
        }

        const Flag flag = splitFlag(args[i]);

        std::string* target = nullptr;
        if (flag.name == "--test-file") {
            target = &options.testFile;
        } else if (flag.name == "--out-file") {
            target = &options.outFile;
        } else {
            return error("unknown option: " + args[i]);
        }

        if (flag.inlineValue) {
            *target = *flag.inlineValue;
        } else if (i + 1 < args.size()) {
            *target = args[++i];
        } else {
            return error("missing value for " + flag.name);
        }
    }

    std::string missing;
    if (options.testFile.empty()) missing += " --test-file";
    if (options.outFile.empty()) missing += " --out-file";
    if (!missing.empty()) {
        return error("missing required option(s):" + missing);
    }

    return {.kind = ParsedCli::Kind::Map, .map = options};
}

ParsedCli parseVisualizeServe(const std::vector<std::string>& args,
                              std::size_t start) {
    VisualizeOptions options;

    for (std::size_t i = start; i < args.size(); ++i) {
        if (isHelpFlag(args[i])) {
            return help();
        }

        const Flag flag = splitFlag(args[i]);

        std::string value;
        std::string* target = nullptr;
        if (flag.name == "--map-file") {
            target = &options.mapFile;
        } else if (flag.name == "--profile-file") {
            target = &options.profileFile;
        } else if (flag.name == "--db-file") {
            target = &options.dbFile;
        } else if (flag.name == "--port") {
            target = &value;
        } else {
            return error("unknown option: " + args[i]);
        }

        if (flag.inlineValue) {
            *target = *flag.inlineValue;
        } else if (i + 1 < args.size()) {
            *target = args[++i];
        } else {
            return error("missing value for " + flag.name);
        }

        if (flag.name == "--port") {
            try {
                options.port = std::stoi(value);
            } catch (...) {
                return error("--port must be an integer");
            }
            if (options.port < 0 || options.port > 65535) {
                return error("--port must be between 0 and 65535");
            }
        }
    }

    if (options.mapFile.empty()) {
        return error("missing required option: --map-file");
    }
    return {.kind = ParsedCli::Kind::VisualizeServe, .visualize = options};
}

ParsedCli parseVisualize(const std::vector<std::string>& args,
                         std::size_t start) {
    if (start >= args.size()) {
        return error("visualize requires a subcommand (serve)");
    }
    if (isHelpFlag(args[start])) {
        return help();
    }
    if (args[start] == "serve") {
        return parseVisualizeServe(args, start + 1);
    }
    return error("unknown visualize subcommand: " + args[start]);
}

}  // namespace

std::string usageText() {
    return "Usage: sqlinsite profile  --test-file <db> --statements <db> "
           "--out-file <csv>\n"
           "                          [--timing raw|relative] [--quiet]\n"
           "       sqlinsite map      --test-file <db> --out-file <db>\n"
           "       sqlinsite visualize serve --map-file <db> "
           "[--profile-file <csv>] [--db-file <db>] [--port <n>]\n"
           "       sqlinsite --help\n";
}

ParsedCli parseCli(const std::vector<std::string>& args) {
    if (args.empty()) {
        return error("no command given");
    }
    if (isHelpFlag(args[0])) {
        return help();
    }
    if (args[0] == "profile") {
        return parseProfile(args, 1);
    }
    if (args[0] == "map") {
        return parseMap(args, 1);
    }
    if (args[0] == "visualize") {
        return parseVisualize(args, 1);
    }
    return error("unknown command: " + args[0]);
}
