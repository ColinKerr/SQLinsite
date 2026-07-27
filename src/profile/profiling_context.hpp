#pragma once

#include <cstdint>
#include <string>

class AccessSink;

// Bridges the VFS layer (which knows nothing about statements) to the profile
// loop. The profile command updates the fields before executing each statement;
// the VFS IO methods read them when emitting rows. Logging only happens while
// `out` is non-null, which the profile command toggles around measured work.
struct ProfilingContext {
    std::string sessionName;
    int statementIndex = 0;  // 0-based index within the session
    int pageSize = 0;        // discovered from the DB header before measuring
    AccessSink* out = nullptr;

    // When true, logged timestamps are rebased to `timeBaseline` so they start
    // near zero (see --timing relative).
    bool relativeTiming = false;
    std::int64_t timeBaseline = 0;
};

// Process-global context shared with the registered VFS.
inline ProfilingContext& profilingContext() {
    static ProfilingContext ctx;
    return ctx;
}
