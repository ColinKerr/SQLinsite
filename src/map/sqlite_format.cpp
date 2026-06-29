#include "map/sqlite_format.hpp"

#include <cstring>

namespace sqlfmt {

std::uint32_t readBE16(const Byte* p) {
    return (std::uint32_t(p[0]) << 8) | p[1];
}

std::uint32_t readBE24(const Byte* p) {
    return (std::uint32_t(p[0]) << 16) | (std::uint32_t(p[1]) << 8) | p[2];
}

std::uint32_t readBE32(const Byte* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
           (std::uint32_t(p[2]) << 8) | p[3];
}

Varint readVarint(const Byte* p, std::size_t maxLen) {
    std::uint64_t result = 0;
    const int limit = static_cast<int>(maxLen < 9 ? maxLen : 9);
    for (int i = 0; i < limit; ++i) {
        const Byte b = p[i];
        if (i == 8) {
            result = (result << 8) | b;
            return {result, 9};
        }
        result = (result << 7) | (b & 0x7f);
        if ((b & 0x80) == 0) {
            return {result, i + 1};
        }
    }
    return {result, limit};
}

std::uint64_t serialTypeSize(std::uint64_t serialType) {
    switch (serialType) {
        case 0:
        case 8:
        case 9:
        case 10:
        case 11:
            return 0;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 3;
        case 4:
            return 4;
        case 5:
            return 6;
        case 6:
        case 7:
            return 8;
        default:
            return (serialType - (serialType % 2 == 0 ? 12 : 13)) / 2;
    }
}

CellValue decodeValue(std::uint64_t serialType, const Byte* p,
                      std::size_t avail) {
    CellValue v;
    const std::uint64_t need = serialTypeSize(serialType);
    const bool partial = need > avail;

    switch (serialType) {
        case 0:
            v.type = CellValue::Type::Null;
            return v;
        case 8:
            v.type = CellValue::Type::Int;
            v.intValue = 0;
            return v;
        case 9:
            v.type = CellValue::Type::Int;
            v.intValue = 1;
            return v;
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6: {
            v.type = CellValue::Type::Int;
            if (partial) {
                v.truncated = true;
                return v;
            }
            std::int64_t value = (p[0] & 0x80) ? -1 : 0;  // sign-extend
            for (std::uint64_t i = 0; i < need; ++i) {
                value = (value << 8) | p[i];
            }
            v.intValue = value;
            return v;
        }
        case 7: {
            v.type = CellValue::Type::Real;
            if (partial) {
                v.truncated = true;
                return v;
            }
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; ++i) {
                bits = (bits << 8) | p[i];
            }
            std::memcpy(&v.realValue, &bits, sizeof(double));
            return v;
        }
        default: {
            const bool text = (serialType % 2) == 1;
            v.type = text ? CellValue::Type::Text : CellValue::Type::Blob;
            v.byteSize = static_cast<std::size_t>(need);
            v.truncated = partial;
            if (text) {
                const std::size_t take = partial ? avail : need;
                v.text.assign(reinterpret_cast<const char*>(p), take);
            }
            return v;
        }
    }
}

RecordHeader parseRecordHeader(const Byte* payload, std::size_t len) {
    RecordHeader header;
    if (len == 0) {
        return header;
    }
    const Varint hs = readVarint(payload, len);
    header.headerSize = hs.value;

    std::size_t pos = hs.length;
    const std::size_t end = hs.value < len ? hs.value : len;
    while (pos < end) {
        const Varint st = readVarint(payload + pos, len - pos);
        header.serialTypes.push_back(st.value);
        pos += st.length;
        if (st.length == 0) {
            break;
        }
    }
    return header;
}

bool isBtreeType(Byte type) {
    return type == 2 || type == 5 || type == 10 || type == 13;
}

BtreeHeader parseBtreeHeader(const Byte* page, std::size_t headerOffset) {
    const Byte* h = page + headerOffset;
    BtreeHeader bh;
    bh.type = h[0];
    bh.firstFreeblock = static_cast<int>(readBE16(h + 1));
    bh.cellCount = static_cast<int>(readBE16(h + 3));
    bh.cellContentStart = static_cast<int>(readBE16(h + 5));
    bh.fragmentedFreeBytes = h[7];
    const bool interior = (bh.type == 2 || bh.type == 5);
    bh.headerSize = interior ? 12 : 8;
    if (interior) {
        bh.rightmostPointer = readBE32(h + 8);
    }
    return bh;
}

PayloadSplit localPayload(std::uint64_t payloadSize, int usableSize,
                          bool tableLeaf) {
    const std::uint64_t u = static_cast<std::uint64_t>(usableSize);
    const std::uint64_t x =
        tableLeaf ? (u - 35) : ((u - 12) * 64 / 255 - 23);
    if (payloadSize <= x) {
        return {payloadSize, false};
    }
    const std::uint64_t m = (u - 12) * 32 / 255 - 23;
    const std::uint64_t k = m + (payloadSize - m) % (u - 4);
    const std::uint64_t local = (k <= x) ? k : m;
    return {local, true};
}

}  // namespace sqlfmt
