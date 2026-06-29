#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Low-level decoders for the SQLite on-disk format
// (https://sqlite.org/fileformat2.html). These operate on raw page bytes and
// perform no I/O. Page byte offsets here are within a single page buffer.

namespace sqlfmt {

using Byte = std::uint8_t;

// Big-endian fixed-width readers.
std::uint32_t readBE16(const Byte* p);
std::uint32_t readBE24(const Byte* p);
std::uint32_t readBE32(const Byte* p);

// A SQLite variable-length integer (1–9 bytes, big-endian, 7 bits per byte
// except the 9th byte which uses all 8). `length` is bytes consumed.
struct Varint {
    std::uint64_t value;
    int length;
};
Varint readVarint(const Byte* p, std::size_t maxLen);

// Bytes occupied by a value of the given record serial type.
std::uint64_t serialTypeSize(std::uint64_t serialType);

// A decoded record cell value. Blob/text bytes are not retained, only sizes,
// to keep the map output bounded; `truncated` marks a value that spills into an
// overflow page and was only partially available locally.
struct CellValue {
    enum class Type { Null, Int, Real, Text, Blob };
    Type type = Type::Null;
    std::int64_t intValue = 0;
    double realValue = 0.0;
    std::string text;       // Type::Text (may be truncated)
    std::size_t byteSize = 0;  // declared size for Text/Blob
    bool truncated = false;
};

// Decodes one value of `serialType` from up to `avail` bytes at `p`.
CellValue decodeValue(std::uint64_t serialType, const Byte* p, std::size_t avail);

// Record header: the leading varint-prefixed list of serial types.
struct RecordHeader {
    std::uint64_t headerSize = 0;
    std::vector<std::uint64_t> serialTypes;
};
RecordHeader parseRecordHeader(const Byte* payload, std::size_t len);

// B-tree page header. `rightmostPointer` is -1 for leaf pages.
struct BtreeHeader {
    Byte type = 0;             // 2,5,10,13
    int firstFreeblock = 0;
    int cellCount = 0;
    int cellContentStart = 0;  // a stored 0 means 65536
    int fragmentedFreeBytes = 0;
    std::int64_t rightmostPointer = -1;
    int headerSize = 8;        // 8 (leaf) or 12 (interior)
};
bool isBtreeType(Byte type);
BtreeHeader parseBtreeHeader(const Byte* page, std::size_t headerOffset);

// Local (in-page) payload size for a cell, and whether it overflows.
struct PayloadSplit {
    std::uint64_t localBytes;
    bool hasOverflow;
};
PayloadSplit localPayload(std::uint64_t payloadSize, int usableSize,
                          bool tableLeaf);

}  // namespace sqlfmt
