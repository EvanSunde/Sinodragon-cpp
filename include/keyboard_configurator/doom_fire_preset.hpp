#pragma once

#include <random>
#include <string>
#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class DoomFirePreset : public LightingPreset {
public:
    DoomFirePreset();

    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }

private:
    void buildGrid(const KeyboardModel& model);
    void ensurePalette();
    void simulate(double delta_seconds);
    void igniteBaseRow();
    void propagateFlames();
    [[nodiscard]] RgbColor colorForHeat(double heat) const;
    static RgbColor parseHexColor(const std::string& value);

    double speed_{1.0};
    double cooling_{0.05};
    double spark_chance_{0.6};
    double spark_intensity_{1.0};
    double step_interval_{0.015};

    bool grid_built_{false};
    std::size_t rows_{0};
    std::size_t cols_{0};
    std::vector<int> cell_to_key_;
    std::vector<int> key_to_cell_;
    std::vector<double> heat_;
    std::vector<RgbColor> palette_;

    double last_time_{0.0};
    double accumulator_{0.0};

    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
};

}  // namespace kb::cfg
