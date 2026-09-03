// Hyprland backend: reads the compositor's event stream (.socket2.sock) and
// reports every focus change.

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

#include "keyboard_configurator/window_source.hpp"

namespace kb::cfg {

namespace {

std::string envOr(const char* key, const char* fallback) {
    const char* value = std::getenv(key);
    if (value != nullptr && *value != '\0') {
        return std::string(value);
    }
    return std::string(fallback != nullptr ? fallback : "");
}

}  // namespace

bool hyprlandAvailable() {
    return !envOr("HYPRLAND_INSTANCE_SIGNATURE", "").empty();
}

class HyprlandWindowSource : public WindowSource {
public:
    explicit HyprlandWindowSource(std::string events_socket)
        : events_socket_(std::move(events_socket)) {}

    ~HyprlandWindowSource() override { stop(); }

    std::string id() const override { return "hyprland"; }

    void start(Callback callback) override {
        if (thread_.joinable()) {
            return;
        }
        callback_ = std::move(callback);
        stop_.store(false);
        thread_ = std::thread(&HyprlandWindowSource::runLoop, this,
                              events_socket_.empty() ? autoDetectSocket() : events_socket_);
    }

    void stop() override {
        stop_.store(true);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    static std::string autoDetectSocket() {
        const std::string signature = envOr("HYPRLAND_INSTANCE_SIGNATURE", "");
        if (signature.empty()) {
            return {};
        }
        const std::string runtime_dir = envOr("XDG_RUNTIME_DIR", "");
        if (!runtime_dir.empty()) {
            return runtime_dir + "/hypr/" + signature + "/.socket2.sock";
        }
        return "/tmp/hypr/" + signature + "/.socket2.sock";
    }

private:
    static int connectSocket(const std::string& path) {
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

    void handleLine(const std::string& line) {
        // "activewindow>>class,title" -- the title may itself contain commas,
        // so only the first one separates the two fields.
        static constexpr const char* kPrefix = "activewindow>>";
        if (line.rfind(kPrefix, 0) != 0) {
            return;
        }
        const std::string payload = line.substr(std::strlen(kPrefix));

        WindowInfo info;
        const auto comma = payload.find(',');
        if (comma == std::string::npos) {
            info.window_class = payload;
        } else {
            info.window_class = payload.substr(0, comma);
            info.title = payload.substr(comma + 1);
        }

        if (callback_) {
            callback_(info);
        }
    }

    void runLoop(std::string socket_path) {
        while (!stop_.load()) {
            const int fd = connectSocket(socket_path);
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                continue;
            }

            // Time the read out so the stop flag is checked regularly.
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200 * 1000;
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            std::string buffer;
            buffer.reserve(4096);
            char chunk[1024];

            while (!stop_.load()) {
                const ssize_t n = ::read(fd, chunk, sizeof(chunk));
                if (n <= 0) {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                        continue;
                    }
                    break;  // reconnect
                }
                buffer.append(chunk, chunk + n);

                std::size_t start = 0;
                for (;;) {
                    const auto newline = buffer.find('\n', start);
                    if (newline == std::string::npos) {
                        buffer.erase(0, start);
                        break;
                    }
                    handleLine(buffer.substr(start, newline - start));
                    start = newline + 1;
                }
            }

            ::close(fd);
            if (!stop_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
    }

    std::string events_socket_;
    Callback callback_;
    std::atomic<bool> stop_{true};
    std::thread thread_;
};

std::unique_ptr<WindowSource> makeHyprlandWindowSource(const std::string& events_socket) {
    return std::make_unique<HyprlandWindowSource>(events_socket);
}

}  // namespace kb::cfg
