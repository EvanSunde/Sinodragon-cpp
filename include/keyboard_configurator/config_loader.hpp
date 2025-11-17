#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/preset_registry.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

struct RuntimeConfig {
    KeyboardModel model;
    std::unique_ptr<DeviceTransport> transport;
    std::vector<std::unique_ptr<LightingPreset>> presets;
    std::vector<ParameterMap> preset_parameters;
    std::chrono::milliseconds frame_interval{std::chrono::milliseconds{33}};
    std::optional<std::uint16_t> interface_usage_page;
    std::optional<std::uint16_t> interface_usage;
};

class ConfigLoader {
public:
    explicit ConfigLoader(const PresetRegistry& registry);

    [[nodiscard]] RuntimeConfig loadFromFile(const std::string& path) const;

private:
    const PresetRegistry& registry_;
};

}  // namespace kb::cfg
