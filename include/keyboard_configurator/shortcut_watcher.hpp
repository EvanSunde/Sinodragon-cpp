#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct libevdev; // forward declaration

#include "keyboard_configurator/config_loader.hpp"

namespace kb::cfg {

class ConfiguratorCLI;
class KeyboardModel;

class ShortcutWatcher {
public:
    ShortcutWatcher(const KeyboardModel& model,
                    ConfiguratorCLI& cli,
                    const HyprConfig& hypr,
                    std::size_t key_count);
    ~ShortcutWatcher();

    void start();
    void stop();

    // Called from Hyprland watcher when active window class changes
    void setActiveClass(const std::string& klass);

private:
    const KeyboardModel& model_;
    ConfiguratorCLI& cli_;
    const HyprConfig hypr_;
    std::size_t key_count_;

    struct CompiledProfile {
        // modifier mask -> key indices
        std::unordered_map<int, std::vector<std::size_t>> combos;
    };

    std::unordered_map<std::string, CompiledProfile> compiled_;

    std::atomic<bool> stop_{false};
    std::thread thread_;

    // State
    std::string active_class_;
    std::string active_shortcut_name_;
    int overlay_index_ {-1};

    // current modifier state bits: 1=CTRL,2=SHIFT,4=ALT,8=SUPER
    std::atomic<int> mods_{0};

    // device handles
    struct DevHandle {
        int fd{-1};
        struct libevdev* dev{nullptr};
        int mask{0};
    };
    std::vector<DevHandle> devices_;

    void runLoop();
    void openDevices();
    void closeDevices();

    void updateActiveShortcutFromClass();
    void applyMaskForMods(int modmask);
};

} // namespace kb::cfg
