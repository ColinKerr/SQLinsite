#pragma once

#include <string>

struct ProfileOptions {
    std::string testFile;
    std::string statementsFile;
    std::string outFile;
    bool relativeTiming = false;  // --timing relative
    bool quiet = false;           // --quiet: suppress the run summary
};

// Runs the profile command end to end. Returns a process exit code
// (0 on success, non-zero on failure). Errors are reported to stderr.
int runProfile(const ProfileOptions& options);
