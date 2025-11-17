#pragma once

#include <unordered_map>
#include <string>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class KeyMapPreset : public LightingPreset {
public:
    KeyMapPreset();

    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return false; }

private:
    std::unordered_map<std::string, RgbColor> label_colors_;
    RgbColor background_{};
};

}  // namespace kb::cfg
