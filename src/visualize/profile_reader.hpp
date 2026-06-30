#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PageAccess {
    std::int64_t pageNumber = 0;
    std::int64_t reads = 0;
    std::int64_t writes = 0;
};

// One (Session Name, Statement Index) pair from the profile, with a stable id
// assigned in first-seen order. These are the leaves the front-end's
// session/query checkbox tree toggles.
struct ProfileLeaf {
    int leafId = 0;
    std::string sessionName;
    std::int64_t statementIndex = 0;
};

// Read/write counts for one leaf on one page.
struct LeafPageAccess {
    int leafId = 0;
    std::int64_t pageNumber = 0;
    std::int64_t reads = 0;
    std::int64_t writes = 0;
};

struct ProfileAggregate {
    std::vector<PageAccess> pages;          // sorted by pageNumber (totals across leaves)
    std::vector<ProfileLeaf> leaves;        // session/statement manifest, first-seen order
    std::vector<LeafPageAccess> leafPages;  // per-(leaf, page) counts, sorted by (leafId, pageNumber)
    std::int64_t totalReads = 0;
    std::int64_t totalWrites = 0;
};

// Parses a sqlinsite profile CSV (quote-aware) and aggregates read/write counts
// per 1-based page number, and per (session, statement) leaf.
ProfileAggregate aggregateProfileCsv(const std::string& csvText);

// Reads and aggregates a profile CSV file. Throws std::runtime_error if the
// file cannot be read.
ProfileAggregate aggregateProfileFile(const std::string& path);

// Serializes an aggregate to the /api/profile JSON shape.
std::string profileJson(const ProfileAggregate& agg);
