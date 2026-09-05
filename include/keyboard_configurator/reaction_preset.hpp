#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Reaction test: after a random wait one key lights up; hit it as fast as you
// can. Your time is shown as a bar -- shorter and greener is better -- and
// jumping the gun before the light counts as a false start.
class ReactionPreset : public GamePreset {
public:
    std::string id() const override { return "reaction"; }
    std::string gameName() const override { return "reaction"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

    // Exposed so the daemon can report your best time.
    [[nodiscard]] double bestSeconds() const { return best_seconds_; }

private:
    enum class Phase { Waiting, Lit, Result, FalseStart };

    void arm(double now);

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    Phase phase_{Phase::Waiting};
    double phase_started_{0.0};
    double lit_at_{0.0};

    std::size_t target_key_{0};
    int target_x_{0};
    int target_y_{0};

    double last_seconds_{0.0};
    double best_seconds_{0.0};

    double min_wait_{1.5};
    double max_wait_{5.0};
    double result_seconds_{2.5};
    // A time at or above this fills the whole bar.
    double slow_seconds_{0.6};

    RgbColor color_target_{80, 255, 120};
    RgbColor color_fast_{60, 255, 90};
    RgbColor color_slow_{255, 80, 40};
    RgbColor color_false_start_{255, 40, 30};
    RgbColor color_background_{0, 0, 8};
};

}  // namespace kb::cfg
