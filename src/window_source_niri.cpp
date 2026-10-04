// niri backend, speaking niri's line-based JSON IPC over $NIRI_SOCKET.
//
// Protocol: connect, write the request `"EventStream"` on one line, read one
// reply line -- {"Ok":"Handled"} -- and then read events, one JSON object per
// line, until the socket closes. Requests are externally tagged enums, so
// `EventStream` is a bare JSON string, and so is every event's variant name:
//
//   {"WindowFocusChanged":{"id":7}}
//   {"WindowOpenedOrChanged":{"window":{"id":7,"app_id":"firefox",...}}}
//
// Unlike Hyprland and sway, niri reports a focus change as a bare window id
// with no app id or title attached, so this keeps a table of id -> app_id and
// title and resolves the id against it. The table cannot be empty when the
// first focus event arrives: niri replays its entire state as events (a
// WindowsChanged carrying every open window) the moment it accepts the event
// stream, before anything live reaches us.

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "keyboard_configurator/json_lite.hpp"
#include "keyboard_configurator/window_source.hpp"

namespace kb::cfg {

namespace {

// A desynchronised stream would otherwise buffer without bound. A
// WindowsChanged listing hundreds of windows is a few hundred kilobytes, so
// this is far above anything legitimate.
constexpr std::size_t kMaxPendingBytes = 4u * 1024u * 1024u;

std::string socketPath() {
    const char* value = std::getenv("NIRI_SOCKET");
    return (value != nullptr) ? std::string(value) : std::string{};
}

bool writeAll(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, bytes + written, size - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
        } else if (!(n < 0 && errno == EINTR)) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool niriAvailable() {
    return !socketPath().empty();
}

class NiriWindowSource : public WindowSource {
public:
    explicit NiriWindowSource(std::string socket_path) : socket_path_(std::move(socket_path)) {}

    ~NiriWindowSource() override { stop(); }

    std::string id() const override { return "niri"; }

    void start(Callback callback) override {
        if (thread_.joinable()) {
            return;
        }
        callback_ = std::move(callback);
        stop_.store(false);
        thread_ = std::thread(&NiriWindowSource::runLoop, this);
    }

    void stop() override {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    struct Entry {
        std::string app_id;
        std::string title;
    };

    int connectSocket() const {
        const std::string path = socket_path_.empty() ? socketPath() : socket_path_;
        if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
            return -1;
        }
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

    // Reads one window object into the table, returning its id.
    std::optional<long long> absorbWindow(const std::string& text, std::size_t object) {
        const auto id = json_lite::intField(text, "id", object);
        if (!id) {
            return std::nullopt;
        }
        Entry entry;
        // Both are Option<String> in niri and really do arrive as null, e.g.
        // for a window that has not set an app id yet.
        entry.app_id = json_lite::stringField(text, "app_id", object).value_or(std::string{});
        entry.title = json_lite::stringField(text, "title", object).value_or(std::string{});
        windows_[*id] = std::move(entry);
        return id;
    }

    void emitFocused() {
        if (!focused_) {
            return;
        }
        const auto it = windows_.find(*focused_);
        if (it == windows_.end() || it->second.app_id.empty()) {
            // No app id means no class to match a profile against; the other
            // backends skip these too.
            return;
        }

        // niri re-announces a window whenever its title changes, and replays
        // its whole state on reconnect. Both would otherwise recompose the
        // keyboard for a window that has not actually changed.
        if (last_class_ == it->second.app_id && last_title_ == it->second.title) {
            return;
        }
        last_class_ = it->second.app_id;
        last_title_ = it->second.title;

        if (callback_) {
            callback_(WindowInfo{it->second.app_id, it->second.title});
        }
    }

    void handleEvent(const std::string& line) {
        const std::size_t root = line.find('{');
        if (root == std::string::npos) {
            return;  // the {"Ok":"Handled"} reply, or a blank line
        }
        const auto tagged = json_lite::firstMember(line, root);
        if (!tagged) {
            return;
        }
        const std::string& tag = tagged->key;
        const std::size_t payload = tagged->value_start;

        if (tag == "WindowsChanged") {
            // A full replacement: anything absent here has been closed.
            const std::size_t array = json_lite::memberValue(line, "windows", payload);
            if (array == std::string::npos || line[array] != '[') {
                return;
            }
            windows_.clear();
            focused_.reset();
            for (const std::size_t object : json_lite::arrayObjects(line, array)) {
                const auto id = absorbWindow(line, object);
                if (id && json_lite::boolField(line, "is_focused", object).value_or(false)) {
                    focused_ = id;
                }
            }
            emitFocused();
            return;
        }

        if (tag == "WindowOpenedOrChanged") {
            const std::size_t object = json_lite::memberValue(line, "window", payload);
            if (object == std::string::npos || line[object] != '{') {
                return;
            }
            const auto id = absorbWindow(line, object);
            // "If the window is focused, all other windows are no longer
            // focused" -- so this doubles as a focus event, and as the way a
            // title change on the focused window reaches us.
            if (id && json_lite::boolField(line, "is_focused", object).value_or(false)) {
                focused_ = id;
                emitFocused();
            }
            return;
        }

        if (tag == "WindowClosed") {
            if (const auto id = json_lite::intField(line, "id", payload)) {
                windows_.erase(*id);
                if (focused_ && *focused_ == *id) {
                    focused_.reset();
                }
            }
            return;
        }

        if (tag == "WindowFocusChanged") {
            // Option<u64>: null when focus went to something that is not a
            // toplevel window, such as a layer-shell panel. Nothing to show
            // then, so the current profile stays up.
            if (const auto id = json_lite::intField(line, "id", payload)) {
                focused_ = id;
                emitFocused();
            } else {
                focused_.reset();
            }
            return;
        }
    }

    // Splits the buffer on newlines; niri writes one event per line.
    void drain(std::string& buffer) {
        std::size_t start = 0;
        for (;;) {
            const std::size_t newline = buffer.find('\n', start);
            if (newline == std::string::npos) {
                break;
            }
            handleEvent(buffer.substr(start, newline - start));
            start = newline + 1;
        }
        buffer.erase(0, start);

        if (buffer.size() > kMaxPendingBytes) {
            std::cerr << "[Window] niri sent " << buffer.size()
                      << " bytes with no line break; dropping it and reconnecting.\n";
            buffer.clear();
            desynchronised_ = true;
        }
    }

    void runLoop() {
        while (!stop_.load()) {
            const int fd = connectSocket();
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            // So a stopped source does not sit in read() for the whole session.
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200 * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            // A unit variant of niri's Request enum, so just its name.
            static constexpr char kRequest[] = "\"EventStream\"\n";
            if (!writeAll(fd, kRequest, sizeof(kRequest) - 1)) {
                ::close(fd);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            // The table and the last-emitted window belong to this connection:
            // niri replays its whole state on the new one, and re-announcing
            // the focused window there is how we resync.
            windows_.clear();
            focused_.reset();
            last_class_.clear();
            last_title_.clear();
            desynchronised_ = false;

            std::string buffer;
            char chunk[4096];
            while (!stop_.load() && !desynchronised_) {
                const ssize_t n = ::read(fd, chunk, sizeof(chunk));
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        continue;  // just the read timeout
                    }
                    break;  // niri exited, or the socket broke
                }
                buffer.append(chunk, chunk + n);
                drain(buffer);
            }

            ::close(fd);
            if (!stop_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    std::string socket_path_;
    Callback callback_;
    std::atomic<bool> stop_{true};
    bool desynchronised_{false};
    std::thread thread_;

    // Touched only by the reader thread.
    std::unordered_map<long long, Entry> windows_;
    std::optional<long long> focused_;
    std::string last_class_;
    std::string last_title_;
};

std::unique_ptr<WindowSource> makeNiriWindowSource(const std::string& socket_path) {
    return std::make_unique<NiriWindowSource>(socket_path);
}

}  // namespace kb::cfg
