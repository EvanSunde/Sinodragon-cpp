#pragma once

namespace kb::cfg {

// Installs SIGINT/SIGTERM/SIGHUP handlers. The handlers do nothing but set a
// flag -- everything else, including blanking the keyboard, happens on the main
// thread once shutdownRequested() is observed. SIGPIPE is ignored so a control
// client that hangs up mid-reply cannot kill the daemon.
void installShutdownHandlers();

// True once a termination signal has been delivered.
[[nodiscard]] bool shutdownRequested();

// The signal number that asked us to stop, or 0.
[[nodiscard]] int shutdownSignal();

}  // namespace kb::cfg
