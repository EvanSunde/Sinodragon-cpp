#pragma once

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class RainbowWavePreset : public LightingPreset {
public:
    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;

private:
    double speed_{0.5};
    double scale_{0.15};
};

}  // namespace kb::cfg
