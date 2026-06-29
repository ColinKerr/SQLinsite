#pragma once

#include <cstdint>

// Derives the 1-based SQLite page number for a main-DB byte offset, matching
// SQLite's own numbering (page 1 is the first page / database header).
// Returns -1 when the page size is not yet known.
inline std::int64_t pageNumberFor(std::int64_t offset, int pageSize) {
    return pageSize > 0 ? offset / pageSize + 1 : -1;
}
