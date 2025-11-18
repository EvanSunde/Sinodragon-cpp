#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/preset_registry.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

struct HyprConfig {
    bool enabled{false};
    std::string events_socket; // optional; if empty, auto-detect
    // Profile-based mapping
    std::string default_profile; // e.g. "Default"
    std::unordered_map<std::string, std::string> class_to_profile; // class -> profile name
    std::unordered_map<std::string, std::vector<bool>> profile_enabled; // profile -> enabled flags
    std::unordered_map<std::string, std::vector<std::vector<bool>>> profile_masks; // profile -> masks per preset
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
