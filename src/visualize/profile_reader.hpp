#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PageAccess {
    std::int64_t pageNumber = 0;
    std::int64_t reads = 0;
    std::int64_t writes = 0;
};

struct ProfileAggregate {
    std::vector<PageAccess> pages;  // sorted by pageNumber
    std::int64_t totalReads = 0;
    std::int64_t totalWrites = 0;
};

// Parses a sqlinsite profile CSV (quote-aware) and aggregates read/write
// counts per 1-based page number.
ProfileAggregate aggregateProfileCsv(const std::string& csvText);

// Reads and aggregates a profile CSV file. Throws std::runtime_error if the
// file cannot be read.
ProfileAggregate aggregateProfileFile(const std::string& path);

// Serializes an aggregate to the /api/profile JSON shape.
std::string profileJson(const ProfileAggregate& agg);
