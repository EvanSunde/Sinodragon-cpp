#include "keyboard_configurator/shutdown_signal.hpp"

#include <csignal>

namespace kb::cfg {

namespace {

// volatile sig_atomic_t is the only thing a handler may portably touch.
volatile std::sig_atomic_t g_shutdown_signal = 0;

extern "C" void handleShutdownSignal(int signal_number) {
    g_shutdown_signal = signal_number;
}

}  // namespace

void installShutdownHandlers() {
    struct sigaction action {};
    action.sa_handler = &handleShutdownSignal;
    sigemptyset(&action.sa_mask);
    // No SA_RESTART: a blocking read in a watcher thread should come back with
    // EINTR so it can notice the shutdown rather than sitting there.
    action.sa_flags = 0;

    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);

    // A control client that disconnects mid-write must not take the daemon
    // with it; the socket write reports EPIPE instead.
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ignore.sa_flags = 0;
    ::sigaction(SIGPIPE, &ignore, nullptr);
}

bool shutdownRequested() {
    return g_shutdown_signal != 0;
}

int shutdownSignal() {
    return static_cast<int>(g_shutdown_signal);
}

}  // namespace kb::cfg
