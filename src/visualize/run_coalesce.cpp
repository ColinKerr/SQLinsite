#include "visualize/run_coalesce.hpp"

std::vector<CoalescedRun> coalesceRuns(const std::vector<PageMeta>& pages) {
    std::vector<CoalescedRun> runs;
    for (const PageMeta& p : pages) {
        if (!runs.empty()) {
            CoalescedRun& last = runs.back();
            if (p.pageNumber == last.endPage + 1 && p.pageType == last.pageType &&
                p.objectId == last.objectId) {
                last.endPage = p.pageNumber;
                continue;
            }
        }
        runs.push_back({p.pageNumber, p.pageNumber, p.pageType, p.objectId});
    }
    return runs;
}
