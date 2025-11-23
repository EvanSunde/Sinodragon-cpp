#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class ReactionDiffusionPreset : public LightingPreset {
public:
    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override {
        key_activity_provider_ = std::move(provider);
    }

private:
    int width_{96};
    int height_{32};
    double du_{0.16};
    double dv_{0.08};
    double feed_{0.035};
    double kill_{0.065};
    int steps_per_frame_{8};
    double zoom_{1.0};
    double speed_{1.0};
    RgbColor color_a_{0,0,0};
    RgbColor color_b_{255,255,255};

    bool inited_{false};
    double last_time_{0.0};
    std::vector<double> u_;
    std::vector<double> v_;

    KeyActivityProviderPtr key_activity_provider_;
    bool reactive_enabled_{true};
    double injection_amount_{0.8};
    double injection_radius_{0.08};
    double injection_decay_{0.6};
    double injection_history_{1.5};

    bool coords_built_{false};
    std::vector<double> xs_;
    std::vector<double> ys_;
    void buildCoords(const KeyboardModel& model);
    void initGrid();
    void step(double dt);
    void applyKeyActivityInjection();
};

}  // namespace kb::cfg
