#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class ReactiveRipplePreset : public LightingPreset {
public:
    std::string id() const override { return "reactive_ripple"; }
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

private:
    void buildCoords(const KeyboardModel& model);
    static RgbColor parseHexColor(const std::string& value);

    KeyActivityProviderPtr provider_;

    double wave_speed_{2.0};
    double decay_time_{1.2};
    double thickness_{0.12};
    double history_window_{2.5};
    double intensity_scale_{1.0};
    RgbColor ripple_color_{0, 170, 255};
    RgbColor base_color_{0, 0, 0};

    bool coords_built_{false};
    std::vector<double> xs_;
    std::vector<double> ys_;
};

}  // namespace kb::cfg
