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
}

void EffectEngine::renderFrame(double time_seconds) {
    if (frame_.size() != model_.keyCount()) {
        frame_.resize(model_.keyCount());
    }

    frame_.fill({0, 0, 0});
    for (auto& preset : presets_) {
        preset->render(model_, time_seconds, frame_);
    }
}

bool EffectEngine::pushFrame() {
    const auto payload = model_.encodeFrame(frame_);
    return transport_.sendFrame(model_, payload);
}

}  // namespace kb::cfg
