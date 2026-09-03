#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Shows a named state that something outside the daemon pushes in:
//
//     sinoctl state build busy      # amber sweep
//     sinoctl state build fail      # red pulse
//     sinoctl state build ok        # green, fading out after a while
//
// The point is a CI script, a git hook or a notification bridge can drive the
// keyboard without any of them needing to know anything about lighting.
class StatusLightPreset : public LightingPreset {
public:
    std::string id() const override { return "status_light"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setSystemState(SystemStatePtr state) override { state_ = std::move(state); }

private:
    enum class Style { Solid, Pulse, Sweep };

    struct Appearance {
        RgbColor color{0, 0, 0};
        Style style{Style::Solid};
    };

    [[nodiscard]] Appearance appearanceFor(const std::string& value) const;

    SystemStatePtr state_;

    std::string signal_{"build"};
    std::vector<std::string> keys_;
    std::vector<std::size_t> resolved_;
    bool resolved_valid_{false};

    std::unordered_map<std::string, Appearance> styles_;
    RgbColor color_off_{0, 0, 0};

    // Seconds after which a settled "ok" fades away. 0 keeps it lit forever.
    double ok_timeout_{20.0};
};

}  // namespace kb::cfg
