#pragma once

#include <vector>

#include "keyboard_configurator/preset.hpp"

namespace kb::cfg {

class StarMatrixPreset : public LightingPreset {
public:
    StarMatrixPreset();

    std::string id() const override;
    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model,
                double time_seconds,
                KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }

private:
    // Parameters
    RgbColor star_color_{};     // default white
    RgbColor background_{};     // default black
    double density_{0.15};      // fraction of keys twinkling at a time
    double speed_{1.5};         // speed of twinkle cycle

    static std::uint32_t hash32(std::uint32_t x);
};

}  // namespace kb::cfg
