#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declaration for the input library struct
struct libevdev;

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

    // Overlay Configuration
    std::size_t overlay_index_{0};
    bool overlay_valid_{false};

    // Pre-compiled mapping of Shortcuts: [ModifierMask -> [KeyIndices...]]
    struct CompiledProfile {
        std::unordered_map<int, std::vector<std::size_t>> combos;
    };
    std::unordered_map<std::string, CompiledProfile> compiled_;

    // Threading
    std::atomic<bool> stop_{false};
    std::thread thread_;

    // State
    std::string active_class_;
    std::string active_shortcut_name_;
    
    // Modifiers state: 1=CTRL, 2=SHIFT, 4=ALT, 8=SUPER
    std::atomic<int> mods_{0};
    bool engaged_{false};

    // Input Devices
    struct Device {
        int fd{-1};
        struct libevdev* dev{nullptr};
        int mask{0};
    };
    std::vector<Device> devices_;

    // Internal Helper Methods
    void runLoop();
    void openDevices();
    void closeDevices();

    void updateActiveShortcutFromClass();
    void applyMaskForMods(int modmask);
    
    // NEW: Restores the background profile based on the active window
    // (Used when releasing Ctrl to switch back to the correct "Painter's List")
    void restoreActiveProfile();
};

} // namespace kb::cfg