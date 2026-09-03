#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace kb::cfg {

class Runtime;

// Where the daemon listens and where sinoctl looks: $XDG_RUNTIME_DIR is the
// right home for a per-user socket, with a /tmp fallback for sessions that
// don't set it.
[[nodiscard]] std::string defaultControlSocketPath();

// A line-oriented unix socket in front of Runtime::execute. Each connection may
// send any number of newline-separated commands; every command's reply is
// written back before the next is read, and the connection closes on EOF.
//
// This is what makes the daemon usable without a terminal: bind a compositor
// key to `sinoctl profile magma` and the CLI becomes optional.
class ControlServer {
public:
    ControlServer(Runtime& runtime, std::string socket_path);
    ~ControlServer();

    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // False if the socket could not be bound; the daemon still runs without it.
    bool start();
    void stop();

    [[nodiscard]] const std::string& path() const noexcept { return socket_path_; }

private:
    void acceptLoop();
    void serveConnection(int client_fd);

    Runtime& runtime_;
    std::string socket_path_;

    int listen_fd_{-1};
    std::atomic<bool> stop_{true};
    std::thread thread_;
};

}  // namespace kb::cfg
