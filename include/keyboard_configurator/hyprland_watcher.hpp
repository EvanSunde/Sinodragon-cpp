#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "keyboard_configurator/config_loader.hpp"

namespace kb::cfg {

class ConfiguratorCLI;

class HyprlandWatcher {
public:
    HyprlandWatcher(HyprConfig cfg, ConfiguratorCLI& cli, std::size_t preset_count);
    ~HyprlandWatcher();

    void start();
    void stop();

private:
    HyprConfig cfg_;
    ConfiguratorCLI& cli_;
    std::size_t preset_count_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::string last_class_;

    static std::string autoDetectEventsSocket();
    void runLoop(std::string socket_path);
};

}  // namespace kb::cfg
