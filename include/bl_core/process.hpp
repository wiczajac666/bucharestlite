#pragma once

#include <functional>
#include <string>
#include <vector>

namespace bl {

struct ProcessResult {
    bool started{false};
    bool cancelled{false};
    int exitCode{-1};
    // Combined stdout/stderr, trimmed to the last few hundred bytes.
    std::string output;
};

// Runs `argv[0]` with the given arguments, without involving a shell. While
// the child runs, `shouldCancel` is polled; a cancelled child is terminated
// (SIGTERM then SIGKILL on POSIX, TerminateProcess on Windows).
ProcessResult runProcess(const std::vector<std::string>& argv,
                         const std::function<bool()>& shouldCancel = {});

} // namespace bl
