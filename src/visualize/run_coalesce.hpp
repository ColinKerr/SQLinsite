#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// One page's run-relevant metadata.
struct PageMeta {
    std::int64_t pageNumber = 0;
    std::string pageType;
    std::optional<std::int64_t> objectId;  // nullopt for structural pages
};

// A maximal contiguous span of pages sharing (pageType, objectId).
struct CoalescedRun {
    std::int64_t startPage = 0;
    std::int64_t endPage = 0;
    std::string pageType;
    std::optional<std::int64_t> objectId;
};

// Coalesces pages (must be sorted ascending by pageNumber, no duplicates) into
// maximal runs: a page extends the current run only when it is exactly one past
// the run's end and shares the same pageType and objectId.
std::vector<CoalescedRun> coalesceRuns(const std::vector<PageMeta>& pages);
