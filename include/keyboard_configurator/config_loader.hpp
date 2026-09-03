#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/preset_registry.hpp"
#include "keyboard_configurator/types.hpp" // Ensure this exists or defines ParameterMap

namespace kb::cfg {

struct ShortcutProfileConfig {
    std::string color;
    std::unordered_map<int, std::vector<std::string>> combos;
};

// A window-title rule. Rules are evaluated in config order and the first
// match wins, which is why this is an ordered vector rather than a map.
struct TitleRule {
    std::string contains;       // matched case-insensitively against the title
    std::string window_class;   // optional: only apply within this class
    std::string profile;
    std::string shortcut;
};

struct HyprConfig {
    bool enabled{false};
    std::string events_socket;
    std::string window_source{"auto"};
    std::string default_profile;
    std::vector<TitleRule> title_rules;
    
    std::unordered_map<std::string, std::string> class_to_profile;
    
    // --- CHANGED: Deprecate the boolean vector, Add the Draw Order vector ---
    // std::unordered_map<std::string, std::vector<bool>> profile_enabled; // Old
    std::unordered_map<std::string, std::vector<std::size_t>> profile_draw_order; // NEW: Painter's List
    
    // Keep masks (they still apply per-layer)
    std::unordered_map<std::string, std::vector<std::vector<bool>>> profile_masks; 
    
    // Keep legacy enabled map for backward compatibility if needed
    std::unordered_map<std::string, std::vector<bool>> profile_enabled;

    int shortcuts_overlay_preset_index{-1};
    std::string default_shortcut;
    std::unordered_map<std::string, std::string> class_to_shortcut;
    std::unordered_map<std::string, ShortcutProfileConfig> shortcuts;
};

struct RuntimeConfig {
    KeyboardModel model;
    std::unique_ptr<DeviceTransport> transport;
    std::vector<std::unique_ptr<LightingPreset>> presets;
    std::vector<ParameterMap> preset_parameters;
    std::chrono::milliseconds frame_interval{std::chrono::milliseconds{33}};
    std::optional<std::uint16_t> interface_usage_page;
    std::optional<std::uint16_t> interface_usage;
    
    std::vector<std::vector<bool>> preset_masks;
    std::vector<bool> preset_enabled;
    std::vector<LayerStyle> preset_styles;

    // Master brightness as a percentage, 0-100.
    int brightness{100};

    // [device] config_watch_mode: start watching the config file immediately
    // rather than waiting for a `watch on` command.
    bool config_watch_mode{false};
    
    std::optional<HyprConfig> hypr;
};

class ConfigLoader {
public:
    explicit ConfigLoader(const PresetRegistry& registry);
    [[nodiscard]] RuntimeConfig loadFromFile(const std::string& path) const;

private:
    const PresetRegistry& registry_;
};

}  // namespace kb::cfg