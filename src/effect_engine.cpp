#include "keyboard_configurator/effect_engine.hpp"

#include <algorithm>
#include <stdexcept>

namespace kb::cfg {

EffectEngine::EffectEngine(const KeyboardModel& model, DeviceTransport& transport)
    : model_(model), transport_(transport), frame_(model.keyCount()) {}

void EffectEngine::setPresets(std::vector<std::unique_ptr<LightingPreset>> presets) {
    presets_ = std::move(presets);
    preset_ids_.clear();
    preset_animated_.clear();
    preset_masks_.clear();
    preset_ids_.reserve(presets_.size());
    preset_animated_.reserve(presets_.size());
    for (const auto& preset : presets_) {
        preset_ids_.push_back(preset->id());
        preset_animated_.push_back(preset->isAnimated());
    }
    frame_.resize(model_.keyCount());
    // Default: only preset 0 enabled, others off
    preset_enabled_.assign(presets_.size(), false);
    if (!preset_enabled_.empty()) {
        preset_enabled_[0] = true;
    }
    // Default masks: all keys affected per preset
    preset_masks_.resize(presets_.size());
    for (auto& mask : preset_masks_) {
        mask.assign(model_.keyCount(), true);
    }
}

void EffectEngine::setPresets(std::vector<std::unique_ptr<LightingPreset>> presets,
                              std::vector<std::vector<bool>> masks) {
    setPresets(std::move(presets));
    // Override masks if provided and sized correctly
    if (masks.size() == preset_masks_.size()) {
        const auto kc = model_.keyCount();
        for (std::size_t i = 0; i < masks.size(); ++i) {
            if (masks[i].size() == kc) {
                preset_masks_[i] = std::move(masks[i]);
            }
        }
    }
}

void EffectEngine::renderFrame(double time_seconds) {
    if (frame_.size() != model_.keyCount()) {
        frame_.resize(model_.keyCount());
    }

    frame_.fill({0, 0, 0});
    KeyColorFrame temp(model_.keyCount());
    for (std::size_t idx = 0; idx < presets_.size(); ++idx) {
        if (!preset_enabled_.empty() && !preset_enabled_[idx]) {
            continue;
        }
        // Render into a temporary frame, then apply masked overlay
        temp.resize(model_.keyCount());
        temp.fill({0, 0, 0});
        presets_[idx]->render(model_, time_seconds, temp);
        const auto& mask = (idx < preset_masks_.size()) ? preset_masks_[idx] : std::vector<bool>();
        if (!mask.empty()) {
            const auto kc = model_.keyCount();
            for (std::size_t k = 0; k < kc; ++k) {
                if (mask[k]) {
                    frame_.setColor(k, temp.color(k));
                }
            }
        } else {
            // No mask provided, apply all
            const auto kc = model_.keyCount();
            for (std::size_t k = 0; k < kc; ++k) {
                frame_.setColor(k, temp.color(k));
            }
        }
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

bool EffectEngine::hasAnimatedEnabled() const {
    for (std::size_t i = 0; i < presets_.size(); ++i) {
        const bool enabled = preset_enabled_.empty() ? true : preset_enabled_[i];
        const bool animated = preset_animated_.size() > i ? preset_animated_[i] : presets_[i]->isAnimated();
        if (enabled && animated) {
            return true;
        }
    }
    return false;
}

void EffectEngine::setPresetMask(std::size_t index, const std::vector<bool>& mask) {
    if (index >= preset_masks_.size()) {
        throw std::out_of_range("EffectEngine::setPresetMask index out of range");
    }
    const auto kc = model_.keyCount();
    if (mask.size() != kc) {
        throw std::invalid_argument("EffectEngine::setPresetMask mask size mismatch");
    }
    preset_masks_[index] = mask;
}

void EffectEngine::setPresetMasks(const std::vector<std::vector<bool>>& masks, bool overlay_replace) {
    (void)overlay_replace; // reserved for future blending semantics
    const auto pc = presets_.size();
    if (masks.size() != pc) {
        return; // ignore mismatched sets
    }
    const auto kc = model_.keyCount();
    for (std::size_t i = 0; i < pc; ++i) {
        if (masks[i].size() == kc) {
            preset_masks_[i] = masks[i];
        }
    }
}

}  // namespace kb::cfg
