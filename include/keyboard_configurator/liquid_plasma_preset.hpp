#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class LiquidPlasmaPreset : public LightingPreset {
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
    double speed_{0.6};
    double scale_{2.5};
    double saturation_{0.9};
    double value_{1.0};
    int wave_complexity_{4}; // 1..10
    enum class MixMode { Linear, Nearest };
    MixMode mix_mode_{MixMode::Linear};
    std::vector<RgbColor> palette_{}; // up to 10 colors

    bool reactive_enabled_{false};
    double reactive_history_{1.2};
    double reactive_decay_{0.35};
    double reactive_spread_{0.12};
    double reactive_intensity_{1.0};
    double reactive_displacement_{0.18};
    double reactive_phase_shift_{1.0};
    double reactive_push_duration_{0.2};
    bool reactive_push_{false};

    bool coords_built_{false};
    std::vector<double> xs_;
    std::vector<double> ys_;
    void buildCoords(const KeyboardModel& model);
    bool computeReactiveFields(std::vector<double>& disp_x,
                               std::vector<double>& disp_y,
                               std::vector<double>& phase_shift);
};

}  // namespace kb::cfg
