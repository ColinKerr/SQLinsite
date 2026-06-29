#include "profile/csv_writer.hpp"

#include <ostream>
#include <stdexcept>

namespace {

// Streams a CSV field, quoting/escaping only when required. The common path
// (no special characters) writes the value directly with no heap allocation.
void streamField(std::ostream& out, const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        out << value;
        return;
    }
    out << '"';
    for (char c : value) {
        if (c == '"') {
            out << '"';
        }
        out << c;
    }
    out << '"';
}

}  // namespace

CsvWriter::CsvWriter(const std::string& path) : buffer_(1 << 20) {
    out_.rdbuf()->pubsetbuf(buffer_.data(), static_cast<std::streamsize>(buffer_.size()));
    out_.open(path, std::ios::binary);
    if (!out_) {
        throw std::runtime_error("cannot open output file: " + path);
    }
}

void CsvWriter::writeHeader() {
    out_ << "Session Name,Statement Index,Time Start,Time End,Page Number,"
            "Read or Write\n";
}

void CsvWriter::writeRow(const std::string& sessionName,
                         int statementIndex,
                         std::int64_t timeStart,
                         std::int64_t timeEnd,
                         std::int64_t pageNumber,
                         AccessType access) {
    streamField(out_, sessionName);
    out_ << ',' << statementIndex << ',' << timeStart << ',' << timeEnd << ','
         << pageNumber << ','
         << (access == AccessType::Read ? "Read" : "Write") << '\n';
    if (access == AccessType::Read) {
        ++reads_;
    } else {
        ++writes_;
    }
}
