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
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override { provider_ = std::move(provider); }

private:
    KeyActivityProviderPtr provider_;
    double speed_{0.2};
    double scale_{2.0};
    int octaves_{4};
    double persistence_{0.5};
    double lacunarity_{2.0};
    double drift_x_{0.0};
    double drift_y_{0.0};
    double contrast_{1.0};
    RgbColor color_low_{0, 0, 0};
    RgbColor color_high_{255, 180, 80};

    bool reactive_enabled_{false};
    double reactive_history_{1.2};
    double reactive_decay_{0.45};
    double reactive_spread_{0.18};
    double reactive_intensity_{1.0};
    double reactive_displacement_{0.35};
    double reactive_push_duration_{0.2};
    bool reactive_push_{false};

    bool coords_built_{false};
    std::vector<double> xs_;
    std::vector<double> ys_;
    void buildCoords(const KeyboardModel& model);
    void computeReactiveDisplacement(std::vector<double>& dx, std::vector<double>& dy);
};

}  // namespace kb::cfg
