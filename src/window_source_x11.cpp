// X11 backend, and the fallback for desktops with no focus-tracking protocol
// of their own: KDE and GNOME on Xorg land here, as does anything running
// under Xwayland with a root window.
//
// It selects PropertyChangeMask on the root window and waits for
// _NET_ACTIVE_WINDOW to change, then reads WM_CLASS and _NET_WM_NAME off the
// newly focused window. Compiled only when libX11 is present at build time.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "keyboard_configurator/window_source.hpp"

#ifdef SINODRAGON_HAVE_X11

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace kb::cfg {

namespace {

// Returns a window property's raw bytes, or an empty string.
std::string readProperty(Display* display, Window window, Atom property, Atom type,
                         unsigned long* count_out = nullptr) {
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long item_count = 0;
    unsigned long bytes_after = 0;
    unsigned char* data = nullptr;

    if (XGetWindowProperty(display, window, property, 0, 1024, False, type, &actual_type,
                           &actual_format, &item_count, &bytes_after, &data) != Success) {
        return {};
    }
    if (data == nullptr) {
        return {};
    }

    std::size_t byte_count = item_count;
    if (actual_format == 16) {
        byte_count *= 2;
    } else if (actual_format == 32) {
        byte_count *= sizeof(long);
    }

    std::string value(reinterpret_cast<char*>(data), byte_count);
    if (count_out != nullptr) {
        *count_out = item_count;
    }
    XFree(data);
    return value;
}

}  // namespace

bool x11Available() {
    const char* display = std::getenv("DISPLAY");
    return display != nullptr && *display != '\0';
}

class X11WindowSource : public WindowSource {
public:
    ~X11WindowSource() override { stop(); }

    std::string id() const override { return "x11"; }

    void start(Callback callback) override {
        if (thread_.joinable()) {
            return;
        }
        callback_ = std::move(callback);
        stop_.store(false);
        thread_ = std::thread(&X11WindowSource::runLoop, this);
    }

    void stop() override {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void report(Display* display, Window window, Atom net_wm_name, Atom utf8) {
        if (window == None) {
            return;
        }

        WindowInfo info;

        // WM_CLASS is two NUL-separated strings: instance then class. The
        // class is the one that matches what other tools report.
        const std::string wm_class = readProperty(display, window, XA_WM_CLASS, XA_STRING);
        if (!wm_class.empty()) {
            const auto separator = wm_class.find('\0');
            info.window_class = (separator == std::string::npos)
                                    ? wm_class
                                    : wm_class.substr(separator + 1, wm_class.find('\0', separator + 1) -
                                                                        separator - 1);
        }

        std::string title = readProperty(display, window, net_wm_name, utf8);
        if (title.empty()) {
            title = readProperty(display, window, XA_WM_NAME, XA_STRING);
        }
        if (const auto nul = title.find('\0'); nul != std::string::npos) {
            title.resize(nul);
        }
        info.title = title;

        if (!info.window_class.empty() && callback_) {
            callback_(info);
        }
    }

    void runLoop() {
        Display* display = XOpenDisplay(nullptr);
        if (display == nullptr) {
            return;
        }

        const Window root = DefaultRootWindow(display);
        const Atom active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
        const Atom net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
        const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);

        XSelectInput(display, root, PropertyChangeMask);

        const auto currentWindow = [&]() -> Window {
            unsigned long count = 0;
            const std::string raw = readProperty(display, root, active_window, XA_WINDOW, &count);
            if (raw.size() < sizeof(long) || count == 0) {
                return None;
            }
            long value = 0;
            std::memcpy(&value, raw.data(), sizeof(long));
            return static_cast<Window>(value);
        };

        Window last = currentWindow();
        report(display, last, net_wm_name, utf8);

        const int fd = ConnectionNumber(display);
        while (!stop_.load()) {
            // Wait on the X connection with a timeout so stop() is prompt.
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(fd, &readable);
            timeval timeout{0, 200 * 1000};

            if (XPending(display) == 0 &&
                ::select(fd + 1, &readable, nullptr, nullptr, &timeout) <= 0) {
                continue;
            }

            bool changed = false;
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                if (event.type == PropertyNotify && event.xproperty.atom == active_window) {
                    changed = true;
                }
            }

            if (!changed) {
                continue;
            }
            const Window now = currentWindow();
            if (now != last) {
                last = now;
                report(display, now, net_wm_name, utf8);
            }
        }

        XCloseDisplay(display);
    }

    Callback callback_;
    std::atomic<bool> stop_{true};
    std::thread thread_;
};

std::unique_ptr<WindowSource> makeX11WindowSource() {
    return std::make_unique<X11WindowSource>();
}

}  // namespace kb::cfg

#else  // !SINODRAGON_HAVE_X11

namespace kb::cfg {

bool x11Available() {
    return false;
}

std::unique_ptr<WindowSource> makeX11WindowSource() {
    return nullptr;
}

}  // namespace kb::cfg

#endif
