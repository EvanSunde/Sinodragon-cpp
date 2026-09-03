#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

// Shows where you actually type: each press adds heat to its key, heat bleeds
// into neighbouring keys and decays over time, and the palette runs cold to
// hot. Deliberately a normal layer rather than a game -- it composites over a
// base effect instead of taking the keyboard over, which is how you would want
// to run it while working.
class TypingHeatmapPreset : public LightingPreset {
public:
    std::string id() const override { return "typing_heatmap"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

private:
    [[nodiscard]] RgbColor colorForHeat(double heat) const;

    KeyActivityProviderPtr provider_;
    double last_poll_{0.0};
    double last_time_{-1.0};

    std::vector<double> heat_;
    std::vector<double> scratch_;

    // Seconds for a key's heat to fall to 1/e of its value.
    double half_life_{12.0};
    double gain_{0.34};
    // How much of a press spreads to physically adjacent keys.
    double spread_{0.18};
    double ceiling_{1.0};

    std::vector<RgbColor> palette_;
    RgbColor color_cold_{0, 0, 0};
};

}  // namespace kb::cfg
