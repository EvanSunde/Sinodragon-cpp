#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "keyboard_configurator/chroma_state.hpp"

namespace kb::cfg {

// The Razer Chroma SDK REST API, served by the daemon itself on
// 127.0.0.1:54235 -- the address every Chroma REST client already knows.
//
// That covers two kinds of game. Ones that speak REST natively reach it
// directly; ones that link RzChromaSDK64.dll reach it through the chroma-shim
// replacement DLL, which turns their calls into the same requests. Either way
// a keyboard frame ends up in ChromaState and `on_frame` fires.
//
// One thread, one poll() over the listening socket and every client: a
// connection only costs anything while it is sending, and with no app
// connected the thread sleeps until one arrives.
//
// Only loopback is bound, and requests carrying an Origin header -- which is
// to say requests a web browser sends on a page's behalf -- are refused, so a
// website cannot drive the keyboard.
class ChromaServer {
public:
    struct Callbacks {
        // A keyboard frame arrived and is in ChromaState.
        std::function<void()> on_frame;
        // Every app that had sent a frame has gone: unregistered, or silent
        // for longer than the session timeout.
        std::function<void()> on_idle;
    };

    ChromaServer(ChromaStatePtr state, int port, std::chrono::milliseconds session_timeout,
                 Callbacks callbacks);
    ~ChromaServer();

    ChromaServer(const ChromaServer&) = delete;
    ChromaServer& operator=(const ChromaServer&) = delete;

    // False when the port could not be bound; the reason is in ChromaState.
    bool start();
    void stop();

    [[nodiscard]] int port() const noexcept { return port_; }

    // Exposed for tests: the response a request would get, without a socket.
    struct Reply {
        int status{200};
        std::string body;
    };
    Reply handleForTest(const std::string& method, const std::string& path, const std::string& body);

private:
    using Clock = std::chrono::steady_clock;

    struct Session {
        std::string title;
        Clock::time_point last_seen{};
        bool sent_frame{false};
    };

    struct Connection {
        int fd{-1};
        std::string in;
        Clock::time_point last_active{};
    };

    struct Request {
        std::string method;
        std::string path;
        std::string body;
        std::string host;
        bool has_origin{false};
        bool keep_alive{true};
    };

    void loop();
    void acceptClients();
    // False when the connection should be closed.
    bool serviceConnection(Connection& connection);
    int pollTimeoutMs(Clock::time_point now) const;

    Reply handle(const Request& request, Clock::time_point now);
    Reply registerApp(const std::string& body, Clock::time_point now);
    Reply keyboard(const std::string& method, const std::string& body, const std::string& session);
    Reply effect(const std::string& method, const std::string& body);
    void apply(const ChromaState::Grid& grid, const std::string& session);

    Session& touch(const std::string& session, Clock::time_point now);
    void expire(Clock::time_point now);
    void updateActivity();
    std::string newEffectId();

    ChromaStatePtr state_;
    int port_;
    std::chrono::milliseconds session_timeout_;
    Callbacks callbacks_;

    int listen_fd_{-1};
    int wake_pipe_[2]{-1, -1};
    std::atomic<bool> running_{false};
    std::thread thread_;

    // Everything below is touched only by the server thread.
    std::vector<Connection> connections_;
    std::unordered_map<std::string, Session> sessions_;
    std::uint64_t next_session_{0};
    bool active_{false};

    // Effects created with POST and applied later with PUT /effect.
    std::unordered_map<std::string, ChromaState::Grid> stored_effects_;
    std::vector<std::string> stored_order_;
    std::uint64_t next_effect_{0};
};

}  // namespace kb::cfg
