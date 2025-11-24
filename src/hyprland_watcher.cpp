#include "keyboard_configurator/hyprland_watcher.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <unistd.h>

#include "keyboard_configurator/configurator_cli.hpp"

namespace kb::cfg {

namespace {
std::string getenv_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    if (v && *v) return std::string(v);
    return std::string(fallback ? fallback : "");
}
}

HyprlandWatcher::HyprlandWatcher(HyprConfig cfg, ConfiguratorCLI& cli, std::size_t preset_count)
    : cfg_(std::move(cfg)), cli_(cli), preset_count_(preset_count) {}

HyprlandWatcher::~HyprlandWatcher() { stop(); }

void HyprlandWatcher::start() {
    if (thread_.joinable()) return;
    stop_.store(false);
    std::string sock = cfg_.events_socket.empty() ? autoDetectEventsSocket() : cfg_.events_socket;
    thread_ = std::thread(&HyprlandWatcher::runLoop, this, sock);
}

void HyprlandWatcher::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string HyprlandWatcher::autoDetectEventsSocket() {
    std::string sig = getenv_or("HYPRLAND_INSTANCE_SIGNATURE", "");
    if (sig.empty()) return {};
    std::string rt = getenv_or("XDG_RUNTIME_DIR", "");
    if (!rt.empty()) {
        return rt + "/hypr/" + sig + "/.socket2.sock";
    }
    return std::string("/tmp/hypr/") + sig + "/.socket2.sock";
}

void HyprlandWatcher::runLoop(std::string socket_path) {
    auto connect_socket = [&](const std::string& path) -> int {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (path.size() >= sizeof(addr.sun_path)) {
            ::close(fd);
            return -1;
        }
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    };

    while (!stop_.load()) {
        int fd = connect_socket(socket_path);
        if (fd < 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // Timeout for 200ms so we can check stop_ flag
        {
            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 200 * 1000; 
            ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        std::string buf;
        buf.reserve(4096);
        char tmp[1024];
        while (!stop_.load()) {
            ssize_t n = ::read(fd, tmp, sizeof(tmp));
            if (n <= 0) {
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    if (stop_.load()) break;
                    continue;
                }
                break; // reconnect
            }
            buf.append(tmp, tmp + n);
            std::size_t pos = 0;
            while (true) {
                auto nl = buf.find('\n', pos);
                if (nl == std::string::npos) {
                    buf.erase(0, pos);
                    break;
                }
                std::string line = buf.substr(pos, nl - pos);
                pos = nl + 1;

                // Protocol: "activewindow>>class,title"
                if (line.rfind("activewindow>>", 0) == 0) {
                    std::string payload = line.substr(std::string("activewindow>>").size());
                    
                    std::string appClass;
                    auto comma = payload.find(',');
                    if (comma != std::string::npos) {
                        appClass = payload.substr(0, comma);
                    } else {
                        appClass = payload;
                    }
                    
                    if (appClass == last_class_) {
                        continue; 
                    }
                    last_class_ = appClass;
                    if (on_class_) { on_class_(last_class_); }

                    // --- Painter's Algorithm Logic ---
                    if (!cfg_.profile_draw_order.empty()) {
                        std::string prof;
                        auto pit = cfg_.class_to_profile.find(appClass);
                        if (pit != cfg_.class_to_profile.end()) {
                            prof = pit->second;
                        } else {
                            prof = cfg_.default_profile;
                        }

                        auto oit = cfg_.profile_draw_order.find(prof);
                        auto mit = cfg_.profile_masks.find(prof);

                        if (oit != cfg_.profile_draw_order.end() && mit != cfg_.profile_masks.end()) {
                            // 1. Get the ordered playlist
                            const std::vector<std::size_t>& draw_list = oit->second;
                            
                            // 2. Get the masks (ensure size safety)
                            std::vector<std::vector<bool>> masks = mit->second;
                            if (masks.size() != preset_count_) {
                                masks.resize(preset_count_, std::vector<bool>());
                            }

                            // 3. Apply to CLI -> Engine
                            cli_.applyPresetMasks(masks);
                            cli_.setDrawList(draw_list); // Calls the new Painter's Algo method
                            cli_.refreshRender();
                            continue;
                        }
                    }
                }
            }
        }
        ::close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace kb::cfg