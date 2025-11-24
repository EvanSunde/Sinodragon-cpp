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

struct HyprConfig {
    bool enabled{false};
    std::string events_socket;
    std::string default_profile;
    
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