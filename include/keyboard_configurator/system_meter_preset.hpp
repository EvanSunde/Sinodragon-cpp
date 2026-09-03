#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Draws a value as a bar across a named run of keys: CPU load along the
// function row, battery along the number row, or anything a script pushes in
// with `sinoctl metric <name> <0..1>`.
//
// The bar fills `bar_keys` in the order they are listed, which is what makes
// "left to right along the F row" mean what you expect regardless of how the
// layout file happens to order keys internally.
//
// Keys outside the bar are painted color_empty (black by default), so stack
// meters with `blend = "add"` -- with the default Normal blend each meter
// would erase the one below it.
class SystemMeterPreset : public LightingPreset {
public:
    std::string id() const override { return "system_meter"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setSystemState(SystemStatePtr state) override { state_ = std::move(state); }

private:
    [[nodiscard]] double sample();
    [[nodiscard]] RgbColor gradient(double position) const;

    SystemStatePtr state_;

    std::string metric_{"cpu"};
    std::string custom_name_;

    std::vector<std::string> bar_keys_;
    std::vector<std::size_t> resolved_;
    bool resolved_valid_{false};

    RgbColor color_low_{0, 220, 90};
    RgbColor color_mid_{240, 200, 0};
    RgbColor color_high_{255, 40, 30};
    RgbColor color_empty_{0, 0, 0};

    // Smoothing, so a spiky metric does not make the keyboard flicker.
    double smoothing_{0.25};
    double smoothed_{-1.0};
    bool invert_{false};
    // Battery only: pulse while charging.
    bool pulse_when_charging_{true};
    bool charging_{false};
};

}  // namespace kb::cfg
