#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

inline std::string tmpPath(const std::string& name) {
    return std::string(SQLINSITE_TEST_TMPDIR) + "/" + name;
}

inline void writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

inline void writeBinaryFile(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

inline std::string readTextFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
