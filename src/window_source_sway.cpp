// sway / i3 backend, speaking the i3 IPC protocol over $SWAYSOCK or $I3SOCK.
//
// Wire format is a fixed header -- the magic "i3-ipc", a little-endian payload
// length and a message type -- followed by JSON. We SUBSCRIBE to "window" and
// then read events; each one carries the focused container, from which we want
// its app_id (Wayland) or window_properties.class (Xwayland) plus its name.

#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "keyboard_configurator/json_lite.hpp"
#include "keyboard_configurator/window_source.hpp"

namespace kb::cfg {

namespace {

constexpr char kMagic[] = "i3-ipc";
constexpr std::size_t kMagicLen = 6;
constexpr std::size_t kHeaderLen = kMagicLen + 8;

constexpr std::uint32_t kTypeSubscribe = 2;
// Event replies have the high bit set; 3 is the window event.
constexpr std::uint32_t kEventWindow = 0x80000003u;

std::string socketPath() {
    for (const char* name : {"SWAYSOCK", "I3SOCK"}) {
        const char* value = std::getenv(name);
        if (value != nullptr && *value != '\0') {
            return std::string(value);
        }
    }
    return {};
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

bool swayAvailable() {
    return !socketPath().empty();
}

class SwayWindowSource : public WindowSource {
public:
    explicit SwayWindowSource(std::string socket_path) : socket_path_(std::move(socket_path)) {}

    ~SwayWindowSource() override { stop(); }

    std::string id() const override { return "sway"; }

    void start(Callback callback) override {
        if (thread_.joinable()) {
            return;
        }
        callback_ = std::move(callback);
        stop_.store(false);
        thread_ = std::thread(&SwayWindowSource::runLoop, this);
    }

    void stop() override {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
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

    static bool sendMessage(int fd, std::uint32_t type, const std::string& payload) {
        char header[kHeaderLen];
        std::memcpy(header, kMagic, kMagicLen);
        const auto length = static_cast<std::uint32_t>(payload.size());
        std::memcpy(header + kMagicLen, &length, 4);
        std::memcpy(header + kMagicLen + 4, &type, 4);
        return writeAll(fd, header, sizeof(header)) && writeAll(fd, payload.data(), payload.size());
    }

    // Pulls whole messages out of the accumulated buffer.
    void drain(std::string& buffer) {
        for (;;) {
            if (buffer.size() < kHeaderLen) {
                return;
            }
            if (std::memcmp(buffer.data(), kMagic, kMagicLen) != 0) {
                buffer.clear();  // desynchronised; the reconnect will resubscribe
                return;
            }

            std::uint32_t length = 0;
            std::uint32_t type = 0;
            std::memcpy(&length, buffer.data() + kMagicLen, 4);
            std::memcpy(&type, buffer.data() + kMagicLen + 4, 4);

            if (buffer.size() < kHeaderLen + length) {
                return;  // wait for the rest
            }

            const std::string payload = buffer.substr(kHeaderLen, length);
            buffer.erase(0, kHeaderLen + length);

            if (type == kEventWindow) {
                handleWindowEvent(payload);
            }
        }
    }

    void handleWindowEvent(const std::string& json) {
        // Only focus changes matter; sway also emits title, move, close, ...
        const auto change = json_lite::stringField(json, "change", json.find('{'));
        if (change && *change != "focus" && *change != "title") {
            return;
        }

        const std::size_t container = json_lite::findObject(json, "container");
        if (container == std::string::npos) {
            return;
        }

        WindowInfo info;
        // Native Wayland windows carry app_id; Xwayland ones only have
        // window_properties.class.
        if (auto app_id = json_lite::stringField(json, "app_id", container)) {
            info.window_class = *app_id;
        } else {
            const std::size_t props = json_lite::findObject(json, "window_properties", container);
            if (props != std::string::npos) {
                if (auto klass = json_lite::stringField(json, "class", props)) {
                    info.window_class = *klass;
                }
            }
        }
        if (auto name = json_lite::stringField(json, "name", container)) {
            info.title = *name;
        }

        if (!info.window_class.empty() && callback_) {
            callback_(info);
        }
    }

    void runLoop() {
        while (!stop_.load()) {
            const int fd = connectSocket();
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200 * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            if (!sendMessage(fd, kTypeSubscribe, R"(["window"])")) {
                ::close(fd);
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            std::string buffer;
            char chunk[4096];
            while (!stop_.load()) {
                const ssize_t n = ::read(fd, chunk, sizeof(chunk));
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        continue;
                    }
                    break;
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
    std::thread thread_;
};

std::unique_ptr<WindowSource> makeSwayWindowSource(const std::string& socket_path) {
    return std::make_unique<SwayWindowSource>(socket_path);
}

}  // namespace kb::cfg
