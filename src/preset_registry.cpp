#include "keyboard_configurator/preset_registry.hpp"

#include <stdexcept>

namespace kb::cfg {

void PresetRegistry::registerPreset(const std::string& id, Factory factory) {
    factories_[id] = std::move(factory);
}

std::unique_ptr<LightingPreset> PresetRegistry::create(const std::string& id) const {
    auto it = factories_.find(id);
    if (it == factories_.end()) {
        throw std::runtime_error("Unknown preset: " + id);
    }
    return it->second();
}

std::vector<std::string> PresetRegistry::listPresetIds() const {
    std::vector<std::string> ids;
    ids.reserve(factories_.size());
    for (const auto& kv : factories_) {
        ids.push_back(kv.first);
    }
    return ids;
}

}  // namespace kb::cfg
