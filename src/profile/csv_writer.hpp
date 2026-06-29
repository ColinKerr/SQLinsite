#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

enum class AccessType { Read, Write };

// Buffered writer for the output CSV described in plan/CLI.md.
class CsvWriter {
public:
    explicit CsvWriter(const std::string& path);

    void writeHeader();

    void writeRow(const std::string& sessionName,
                  int statementIndex,
                  std::int64_t timeStart,
                  std::int64_t timeEnd,
                  std::int64_t pageNumber,
                  AccessType access);

    std::int64_t readCount() const { return reads_; }
    std::int64_t writeCount() const { return writes_; }
    std::int64_t rowCount() const { return reads_ + writes_; }

private:
    std::vector<char> buffer_;
    std::ofstream out_;
    std::int64_t reads_ = 0;
    std::int64_t writes_ = 0;
};
