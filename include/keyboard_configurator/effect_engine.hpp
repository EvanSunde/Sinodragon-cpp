#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class EffectEngine {
public:
    EffectEngine(const KeyboardModel& model, DeviceTransport& transport);

    void setPresets(std::vector<std::unique_ptr<LightingPreset>> presets);
    void renderFrame(double time_seconds);
    bool pushFrame();

    [[nodiscard]] const KeyColorFrame& frame() const noexcept { return frame_; }
    [[nodiscard]] const std::vector<std::string>& presetIds() const noexcept { return preset_ids_; }
    [[nodiscard]] std::size_t presetCount() const noexcept { return presets_.size(); }
    [[nodiscard]] LightingPreset& presetAt(std::size_t index);
    [[nodiscard]] const LightingPreset& presetAt(std::size_t index) const;
    void setPresetEnabled(std::size_t index, bool enabled);
    [[nodiscard]] bool presetEnabled(std::size_t index) const;

private:
    const KeyboardModel& model_;
    DeviceTransport& transport_;
    KeyColorFrame frame_;
    std::vector<std::unique_ptr<LightingPreset>> presets_;
    std::vector<std::string> preset_ids_;
    std::vector<bool> preset_enabled_;
};

}  // namespace kb::cfg
