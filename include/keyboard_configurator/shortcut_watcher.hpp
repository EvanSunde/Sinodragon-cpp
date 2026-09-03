#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// Forward declaration for the input library struct
struct libevdev;

#include "keyboard_configurator/config_loader.hpp"

namespace kb::cfg {

class Runtime;
class KeyboardModel;

class ShortcutWatcher {
public:
    ShortcutWatcher(const KeyboardModel& model,
                    Runtime& runtime,
                    const HyprConfig& hypr,
                    std::size_t key_count);
    ~ShortcutWatcher();

    void start();
    void stop();

    // Called when the focused window changes. Returns true if the overlay is
    // currently engaged, in which case the caller should leave the profile
    // alone until the modifier is released.
    bool setActiveWindow(const std::string& window_class, const std::string& title);

    // Swaps in a freshly loaded config and recompiles the combo tables, so a
    // hot reload does not need the evdev thread restarted.
    void reconfigure(const HyprConfig& hypr);

private:
    const KeyboardModel& model_;
    Runtime& runtime_;
    HyprConfig hypr_;
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
    mutable std::recursive_mutex mutex_;

    // State
    std::string active_class_;
    std::string active_title_;
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

    void compileShortcuts();
    void updateActiveShortcutFromClass();
    void applyMaskForMods(int modmask);
    
    // NEW: Restores the background profile based on the active window
    // (Used when releasing Ctrl to switch back to the correct "Painter's List")
    void restoreActiveProfile();
};

} // namespace kb::cfg