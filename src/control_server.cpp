#include "keyboard_configurator/control_server.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "keyboard_configurator/runtime.hpp"

namespace kb::cfg {

namespace {

constexpr int kAcceptPollMs = 200;
constexpr int kClientIdleTimeoutMs = 5000;
constexpr std::size_t kMaxRequestBytes = 64 * 1024;

bool writeAll(int fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;  // EPIPE: the client hung up. Not our problem.
    }
    return true;
}

// True when something is already listening on this path, so we can tell a live
// daemon apart from a socket left behind by one that crashed.
bool socketIsLive(const std::string& path) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    const bool live = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    ::close(fd);
    return live;
}

}  // namespace

std::string defaultControlSocketPath() {
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    if (runtime_dir != nullptr && *runtime_dir != '\0') {
        return std::string(runtime_dir) + "/sinodragon.sock";
    }
    return "/tmp/sinodragon-" + std::to_string(::getuid()) + ".sock";
}

ControlServer::ControlServer(Runtime& runtime, std::string socket_path)
    : runtime_(runtime), socket_path_(std::move(socket_path)) {}

ControlServer::~ControlServer() {
    stop();
}

bool ControlServer::start() {
    if (!stop_.load()) {
        return true;
    }

    if (socket_path_.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "[Control] Socket path too long: " << socket_path_ << '\n';
        return false;
    }

    if (std::filesystem::exists(socket_path_)) {
        if (socketIsLive(socket_path_)) {
            std::cerr << "[Control] Another daemon is already listening on " << socket_path_ << '\n';
            return false;
        }
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);  // stale socket from a crash
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[Control] socket() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    // Only this user may drive the daemon.
    const mode_t previous_umask = ::umask(0177);
    const int bound = ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::umask(previous_umask);

    if (bound != 0) {
        std::cerr << "[Control] bind(" << socket_path_ << ") failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 8) != 0) {
        std::cerr << "[Control] listen() failed: " << std::strerror(errno) << '\n';
        ::close(listen_fd_);
        listen_fd_ = -1;
        std::error_code ec;
        std::filesystem::remove(socket_path_, ec);
        return false;
    }

    stop_.store(false);
    thread_ = std::thread(&ControlServer::acceptLoop, this);
    std::cout << "[Control] Listening on " << socket_path_ << '\n';
    return true;
}

void ControlServer::stop() {
    if (stop_.exchange(true)) {
        return;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    std::error_code ec;
    std::filesystem::remove(socket_path_, ec);
}

void ControlServer::acceptLoop() {
    while (!stop_.load()) {
        pollfd listener{listen_fd_, POLLIN, 0};
        const int ready = ::poll(&listener, 1, kAcceptPollMs);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            continue;  // timed out; re-check stop_
        }

        const int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            break;
        }
        serveConnection(client_fd);
        ::close(client_fd);
    }
}

void ControlServer::serveConnection(int client_fd) {
    std::string buffer;

    while (!stop_.load()) {
        pollfd client{client_fd, POLLIN, 0};
        const int ready = ::poll(&client, 1, kClientIdleTimeoutMs);
        if (ready <= 0) {
            return;  // idle client or poll error; drop it
        }

        char chunk[1024];
        const ssize_t n = ::read(client_fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (n == 0) {
            break;  // client finished sending
        }

        buffer.append(chunk, chunk + static_cast<std::size_t>(n));
        if (buffer.size() > kMaxRequestBytes) {
            writeAll(client_fd, "Request too large.\n");
            return;
        }

        std::size_t start = 0;
        for (;;) {
            const auto newline = buffer.find('\n', start);
            if (newline == std::string::npos) {
                buffer.erase(0, start);
                break;
            }
            const std::string line = buffer.substr(start, newline - start);
            start = newline + 1;

            const std::string reply = runtime_.execute(line);
            if (!writeAll(client_fd, reply.empty() ? std::string("\n") : reply + "\n")) {
                return;
            }
        }
    }

    // A client that closed without a trailing newline still gets its command run.
    const std::string trailing = buffer;
    if (!trailing.empty()) {
        const std::string reply = runtime_.execute(trailing);
        writeAll(client_fd, reply.empty() ? std::string("\n") : reply + "\n");
    }
    ::shutdown(client_fd, SHUT_WR);
}

}  // namespace kb::cfg
