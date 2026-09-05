#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "keyboard_configurator/preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// A pomodoro timer you can see without switching windows: the bar drains along
// a run of keys as the interval runs down, shifting colour as it goes, and
// flashes when a phase ends.
//
// Driven from the control socket -- `sinoctl pomodoro start|pause|reset|skip`
// -- because a timer needs commands, not just config. It keeps its own clock
// rather than borrowing the render clock, so pausing actually pauses.
class PomodoroPreset : public LightingPreset {
public:
    enum class Phase { Work, ShortBreak, LongBreak };

    std::string id() const override { return "pomodoro"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }

    // --- driven by the `pomodoro` command ---
    void start();
    void pause();
    void reset();
    void skip();  // end the current phase now and move to the next
    [[nodiscard]] std::string statusLine() const;

private:
    using Clock = std::chrono::steady_clock;

    void advancePhase();
    [[nodiscard]] double phaseLength() const;
    [[nodiscard]] double remaining() const;
    [[nodiscard]] RgbColor phaseColor() const;

    std::vector<std::string> bar_keys_;
    std::vector<std::size_t> resolved_;
    bool resolved_valid_{false};

    // Minutes, as everyone states them.
    double work_minutes_{25.0};
    double short_break_minutes_{5.0};
    double long_break_minutes_{15.0};
    int rounds_before_long_break_{4};

    Phase phase_{Phase::Work};
    int completed_work_rounds_{0};

    bool running_{false};
    // Seconds already spent in this phase, frozen while paused.
    double elapsed_in_phase_{0.0};
    Clock::time_point resumed_at_{};

    // A finished phase flashes until acknowledged by starting the next one.
    double flash_seconds_{6.0};
    double phase_ended_at_{-1.0};

    RgbColor color_work_{255, 70, 40};
    RgbColor color_short_break_{40, 200, 255};
    RgbColor color_long_break_{80, 255, 140};
    RgbColor color_spent_{0, 0, 0};
    RgbColor color_paused_{120, 120, 120};
};

}  // namespace kb::cfg
