#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "keyboard_configurator/config_loader.hpp"

namespace kb::cfg {

class Runtime;

class HyprlandWatcher {
public:
    HyprlandWatcher(std::string events_socket, Runtime& runtime);
    ~HyprlandWatcher();

    void start();
    void stop();
    void setActiveClassCallback(std::function<bool(const std::string&)> cb) { on_class_ = std::move(cb); }

private:
    // Holds no config of its own: the runtime resolves the window class using
    // whatever config is current, so a hot reload needs nothing done here.
    std::string events_socket_;
    Runtime& runtime_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::string last_class_;
    std::function<bool(const std::string&)> on_class_;

    static std::string autoDetectEventsSocket();
    void runLoop(std::string socket_path);
};

}  // namespace kb::cfg
