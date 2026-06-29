#pragma once

#include <string>
#include <vector>

struct Session {
    std::string name;
    std::vector<std::string> statements;
};

struct TestRun {
    std::string name;
    std::vector<Session> sessions;
};

// Parses the statements JSON described in plan/CLI.md.
// Throws std::runtime_error with a clear message on malformed or invalid input.
TestRun parseStatementsFile(const std::string& path);
