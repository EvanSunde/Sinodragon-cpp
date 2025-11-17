#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class PresetRegistry {
public:
    using Factory = std::function<std::unique_ptr<LightingPreset>()>;

    void registerPreset(const std::string& id, Factory factory);
    [[nodiscard]] std::unique_ptr<LightingPreset> create(const std::string& id) const;
    [[nodiscard]] std::vector<std::string> listPresetIds() const;

private:
    std::unordered_map<std::string, Factory> factories_;
};

}  // namespace kb::cfg
