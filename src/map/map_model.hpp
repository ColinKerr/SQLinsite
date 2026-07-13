#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "map/db_file.hpp"
#include "map/sqlite_format.hpp"

enum class PageType {
    TableLeaf,
    TableInterior,
    IndexLeaf,
    IndexInterior,
    Overflow,
    FreelistTrunk,
    FreelistLeaf,
    PointerMap,
    LockByte,
    Unallocated,
};

std::string pageTypeName(PageType type);

struct Pointer {
    std::int64_t toPage = 0;
    std::string kind;  // child | overflow | freelist-next | freelist-leaf | ptrmap-parent
};

struct CellInfo {
    std::optional<std::int64_t> rowid;       // table cells
    std::optional<std::int64_t> leftChild;   // interior cells
    std::int64_t payloadBytes = 0;           // declared total payload (leaf/index)
    std::int64_t localBytes = 0;
    std::optional<std::int64_t> overflowPage;
    std::vector<sqlfmt::CellValue> key;      // index cells (decoded key fields)
};

struct PtrmapEntry {
    std::int64_t targetPage = 0;
    int entryType = 0;
    std::int64_t parentPage = 0;
};

struct PageHeaderInfo {
    std::optional<int> firstFreeblock;
    std::optional<int> cellCount;
    std::optional<int> cellContentStart;
    std::optional<int> fragmentedFreeBytes;
    std::optional<std::int64_t> rightmostPointer;
    std::optional<std::int64_t> nextTrunkPage;   // freelist trunk
    std::optional<int> leafCount;                // freelist trunk
    std::optional<std::int64_t> nextOverflowPage;
};

struct PageInfo {
    std::int64_t pageNumber = 0;
    PageType type = PageType::Unallocated;
    std::optional<std::string> object;       // owning object name
    std::optional<std::string> objectType;   // table | index
    PageHeaderInfo header;
    std::int64_t freeBytes = 0;
    std::vector<CellInfo> cells;
    std::vector<Pointer> pointers;
    std::vector<PtrmapEntry> ptrmapEntries;
    std::optional<std::string> parseError;
};

struct ObjectInfo {
    std::string type;        // table | index
    std::string name;
    std::string tableName;   // tbl_name
    std::int64_t rootPage = 0;
    std::string sql;
    std::vector<std::int64_t> pages;
};

struct MapModel {
    std::string path;
    DbHeader database;
    std::int64_t pageCount = 0;
    std::vector<ObjectInfo> objects;
    std::vector<PageInfo> pages;  // ordered by page number, index 0 == page 1
};
