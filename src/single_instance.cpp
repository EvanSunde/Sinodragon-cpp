#include "keyboard_configurator/single_instance.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace kb::cfg {

namespace {

std::string trim(const std::string& in) {
    const auto begin = in.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = in.find_last_not_of(" \t\r\n");
    return in.substr(begin, end - begin + 1);
}

}  // namespace

SingleInstanceLock::~SingleInstanceLock() {
    // Just release the lock; do not unlink (see the header for why).
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SingleInstanceLock::acquire(const std::string& path) {
    path_ = path;

    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) {
        error_ = "cannot open lock file " + path + ": " + std::strerror(errno);
        return false;
    }

    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            // Read the holder's pid for a friendlier message.
            char buf[64] = {0};
            const ssize_t n = ::pread(fd_, buf, sizeof(buf) - 1, 0);
            const std::string pid = (n > 0) ? trim(std::string(buf, static_cast<std::size_t>(n))) : "";
            error_ = "another sinodragon instance is already running";
            if (!pid.empty()) {
                error_ += " (pid " + pid + ")";
            }
        } else {
            error_ = std::string("flock(") + path + ") failed: " + std::strerror(errno);
        }
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Record our pid so the next starter can name us in its refusal message.
    if (::ftruncate(fd_, 0) == 0) {
        const std::string pid = std::to_string(::getpid()) + "\n";
        const ssize_t written = ::pwrite(fd_, pid.data(), pid.size(), 0);
        (void)written;  // purely informational; a short write is harmless
    }
    return true;
}

std::string defaultLockPath(const std::string& socket_path) {
    return socket_path + ".lock";
}

}  // namespace kb::cfg
