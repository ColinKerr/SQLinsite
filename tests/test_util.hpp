#pragma once

#include <fstream>
#include <sstream>
#include <string>

inline std::string tmpPath(const std::string& name) {
    return std::string(SQLINSITE_TEST_TMPDIR) + "/" + name;
}

inline void writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

inline std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
