#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class SmokePreset : public LightingPreset {
public:
    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }

private:
    double speed_{0.2};
    double scale_{2.0};
    int octaves_{4};
    double persistence_{0.5};
    double lacunarity_{2.0};
    RgbColor color_low_{0, 0, 0};
    RgbColor color_high_{255, 180, 80};

    bool coords_built_{false};
    std::vector<double> xs_;
    std::vector<double> ys_;
    void buildCoords(const KeyboardModel& model);
};

}  // namespace kb::cfg
