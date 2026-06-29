#pragma once

#include <cstdint>

#include "map/db_file.hpp"
#include "map/map_model.hpp"

// Parses a single page's bytes into a PageInfo. The owning object is filled in
// later by the map builder; these functions only decode structure.
namespace page_parser {

// Decodes a b-tree page (table/index, leaf/interior) from its header byte.
PageInfo parseBtree(const DbFile& db, std::int64_t pageNumber);

PageInfo parseOverflow(const DbFile& db, std::int64_t pageNumber);
PageInfo parseFreelistTrunk(const DbFile& db, std::int64_t pageNumber);
PageInfo parsePointerMap(const DbFile& db, std::int64_t pageNumber);

// freelist-leaf, lock-byte, unallocated: no internal structure to decode.
PageInfo parseSimple(std::int64_t pageNumber, PageType type);

}  // namespace page_parser
