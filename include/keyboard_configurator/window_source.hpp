#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kb::cfg {

struct WindowInfo {
    std::string window_class;
    std::string title;
};

// A source of "the focused window changed" events. Backends exist for
// Hyprland, sway/i3 and X11; which one runs is decided by the environment
// unless the config names one.
class WindowSource {
public:
    using Callback = std::function<void(const WindowInfo&)>;

    virtual ~WindowSource() = default;

    [[nodiscard]] virtual std::string id() const = 0;

    // Starts watching. The callback runs on the source's own thread.
    virtual void start(Callback callback) = 0;
    virtual void stop() = 0;
};

// True when this backend's compositor appears to be the one running.
[[nodiscard]] bool hyprlandAvailable();
[[nodiscard]] bool swayAvailable();
[[nodiscard]] bool x11Available();

// Backends compiled into this build, best first.
[[nodiscard]] std::vector<std::string> availableWindowSources();

// `preferred` comes from `[hypr] window_source`; "auto" (or empty) picks the
// first backend whose compositor is actually present. Returns nullptr when
// nothing matches, which is not fatal -- the daemon simply stops switching
// profiles by window and can still be driven through sinoctl.
[[nodiscard]] std::unique_ptr<WindowSource> createWindowSource(const std::string& preferred,
                                                               const std::string& events_socket);

}  // namespace kb::cfg
