#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Simon: the board flashes a lengthening sequence of keys and you repeat it.
// The most keyboard-native game there is -- the "buttons" are literally keys,
// each with its own colour.
class SimonPreset : public GamePreset {
public:
    std::string id() const override { return "simon"; }
    std::string gameName() const override { return "simon"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    // Show the sequence, then take input, then celebrate or fail.
    enum class Phase { Playback, Input, Correct, Wrong };

    void restart(double now);
    void extendSequence();
    void handleInput(const KeyboardModel& model, double now);

    GameInput input_;

    bool running_{false};
    Phase phase_{Phase::Playback};
    double phase_started_{0.0};

    std::vector<std::string> pad_keys_{"A", "S", "D", "F"};
    std::vector<std::size_t> pad_indices_;
    std::vector<RgbColor> pad_colors_;
    bool pads_resolved_{false};

    std::vector<int> sequence_;   // indices into pad_keys_
    std::size_t input_position_{0};
    int round_{0};

    double step_seconds_{0.55};   // how long each flash lasts
    double gap_seconds_{0.18};    // dark gap between flashes
    double result_seconds_{1.2};
    double idle_level_{0.12};     // how brightly an unlit pad glows

    RgbColor color_background_{0, 0, 0};
    RgbColor color_wrong_{255, 40, 30};
};

}  // namespace kb::cfg
