#include "profile/statements_file.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("statements file: " + message);
}

const json& require(const json& obj, const char* key) {
    if (!obj.contains(key)) {
        fail(std::string("missing required field '") + key + "'");
    }
    return obj.at(key);
}

}  // namespace

TestRun parseStatementsFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        fail("cannot open " + path);
    }

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        fail(std::string("invalid JSON: ") + e.what());
    }

    const json& testRun = require(root, "TestRun");

    TestRun result;
    result.name = require(testRun, "Name").get<std::string>();

    const json& sessions = require(testRun, "Sessions");
    if (!sessions.is_array() || sessions.empty()) {
        fail("'Sessions' must be a non-empty array");
    }

    for (const json& sessionJson : sessions) {
        Session session;
        session.name = require(sessionJson, "SessionName").get<std::string>();

        const json& statements = require(sessionJson, "Statements");
        if (!statements.is_array() || statements.empty()) {
            fail("session '" + session.name +
                 "' must have a non-empty 'Statements' array");
        }
        for (const json& statement : statements) {
            session.statements.push_back(statement.get<std::string>());
        }
        result.sessions.push_back(std::move(session));
    }

    return result;
}
