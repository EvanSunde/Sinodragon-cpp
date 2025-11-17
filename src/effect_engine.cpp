#include "keyboard_configurator/effect_engine.hpp"

#include <stdexcept>

namespace kb::cfg {

EffectEngine::EffectEngine(const KeyboardModel& model, DeviceTransport& transport)
    : model_(model), transport_(transport), frame_(model.keyCount()) {}

void EffectEngine::setPresets(std::vector<std::unique_ptr<LightingPreset>> presets) {
    presets_ = std::move(presets);
    preset_ids_.clear();
    preset_ids_.reserve(presets_.size());
    for (const auto& preset : presets_) {
        preset_ids_.push_back(preset->id());
    }
    frame_.resize(model_.keyCount());
    preset_enabled_.assign(presets_.size(), true);
}

void EffectEngine::renderFrame(double time_seconds) {
    if (frame_.size() != model_.keyCount()) {
        frame_.resize(model_.keyCount());
    }

    frame_.fill({0, 0, 0});
    for (std::size_t idx = 0; idx < presets_.size(); ++idx) {
        if (!preset_enabled_.empty() && !preset_enabled_[idx]) {
            continue;
        }
        presets_[idx]->render(model_, time_seconds, frame_);
    }
}

bool EffectEngine::pushFrame() {
    const auto payload = model_.encodeFrame(frame_);
    return transport_.sendFrame(model_, payload);
}

LightingPreset& EffectEngine::presetAt(std::size_t index) {
    if (index >= presets_.size()) {
        throw std::out_of_range("EffectEngine::presetAt index out of range");
    }
    return *presets_[index];
}

const LightingPreset& EffectEngine::presetAt(std::size_t index) const {
    if (index >= presets_.size()) {
        throw std::out_of_range("EffectEngine::presetAt index out of range");
    }
    return *presets_[index];
}

void EffectEngine::setPresetEnabled(std::size_t index, bool enabled) {
    if (index >= presets_.size()) {
        throw std::out_of_range("EffectEngine::setPresetEnabled index out of range");
    }
    if (preset_enabled_.size() != presets_.size()) {
        preset_enabled_.assign(presets_.size(), true);
    }
    preset_enabled_[index] = enabled;
}

bool EffectEngine::presetEnabled(std::size_t index) const {
    if (index >= presets_.size()) {
        throw std::out_of_range("EffectEngine::presetEnabled index out of range");
    }
    if (preset_enabled_.empty()) {
        return true;
    }
    return preset_enabled_[index];
}

}  // namespace kb::cfg
