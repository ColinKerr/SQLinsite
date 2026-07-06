#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "profile/access_sink.hpp"

// In-memory AccessSink that aggregates read/write counts per page for one query
// run (the visualize live-query view). Ignores session/statement/timing — a run
// is a single measured execution.
class AggregatingSink : public AccessSink {
public:
    struct Page {
        std::int64_t pageNumber;
        std::int64_t reads;
        std::int64_t writes;
    };

    void record(const std::string&, int, std::int64_t, std::int64_t,
                std::int64_t pageNumber, AccessType access) override {
        RW& a = byPage_[pageNumber];
        if (access == AccessType::Read) { ++a.reads; ++reads_; }
        else { ++a.writes; ++writes_; }
    }

    // Pages touched, ascending by page number.
    std::vector<Page> pages() const {
        std::vector<Page> v;
        v.reserve(byPage_.size());
        for (const auto& [pg, a] : byPage_) v.push_back({pg, a.reads, a.writes});
        return v;
    }

    std::int64_t reads() const { return reads_; }
    std::int64_t writes() const { return writes_; }
    std::int64_t accesses() const { return reads_ + writes_; }
    std::int64_t distinctPages() const { return static_cast<std::int64_t>(byPage_.size()); }

private:
    struct RW { std::int64_t reads = 0; std::int64_t writes = 0; };
    std::map<std::int64_t, RW> byPage_;
    std::int64_t reads_ = 0;
    std::int64_t writes_ = 0;
};
