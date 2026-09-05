#pragma once

#include <string>

namespace kb::cfg {

// An advisory exclusive lock (flock) held for the whole life of the process, so
// a second daemon refuses to start rather than two of them fighting over the
// same keyboard. The kernel releases the lock automatically when the process
// exits -- crash included -- so there is no stale-lock problem to clean up.
//
// The lock file is intentionally never unlinked: unlinking it on exit races
// with a concurrent starter (it could lock a now-deleted inode while a third
// process creates a fresh one), which would let two instances run. Leaving the
// file in place makes its inode the single source of truth.
class SingleInstanceLock {
public:
    SingleInstanceLock() = default;
    ~SingleInstanceLock();

    SingleInstanceLock(const SingleInstanceLock&) = delete;
    SingleInstanceLock& operator=(const SingleInstanceLock&) = delete;

    // Tries to take the lock at `path`. Returns true on success; on failure
    // error() explains why (already running, or the file could not be opened).
    bool acquire(const std::string& path);

    [[nodiscard]] const std::string& error() const noexcept { return error_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    int fd_{-1};
    std::string path_;
    std::string error_;
};

// Where the lock lives, derived from the control socket path so that an
// instance given its own --socket (a second keyboard, say) also gets its own
// lock and is allowed to run alongside the first.
[[nodiscard]] std::string defaultLockPath(const std::string& socket_path);

}  // namespace kb::cfg
