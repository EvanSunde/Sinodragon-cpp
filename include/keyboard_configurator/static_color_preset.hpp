#pragma once

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class StaticColorPreset : public LightingPreset {
public:
    StaticColorPreset();

    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;

private:
    RgbColor color_;
};

}  // namespace kb::cfg
