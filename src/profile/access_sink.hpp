#pragma once

#include <cstdint>
#include <string>

enum class AccessType { Read, Write };

// Receives one page access per read/write while profiling is active. Implemented
// by SqliteWriter (streams rows into the `sqlinsite profile` output db) and by
// AggregatingSink (in-memory per-page totals for the visualize live-query view).
// The VFS holds an AccessSink* in the profiling context and calls record() from
// its IO methods.
class AccessSink {
public:
    virtual ~AccessSink() = default;
    virtual void record(const std::string& sessionName, int statementIndex,
                        std::int64_t timeStart, std::int64_t timeEnd,
                        std::int64_t pageNumber, AccessType access) = 0;
};
