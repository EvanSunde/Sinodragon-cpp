#pragma once

#include <string>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Two-player Pong across the width of the keyboard, which is what a grid six
// rows tall and sixteen wide is actually good for: the left player defends the
// leftmost column, the right player the rightmost, and the two sit at opposite
// ends of the same keyboard.
//
// Defaults put the left player on W/S and the right player on the Up/Down
// arrows -- the natural split for two people sharing one board. Every key is
// configurable, and `opponent = "ai"` brings back a computer right-hand player
// for solo play.
class PongPreset : public GamePreset {
public:
    std::string id() const override { return "pong"; }
    std::string gameName() const override { return "pong"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    void serve(int towards);
    void step(const KeyboardModel& model, double dt);
    void handleInput(const KeyboardModel& model);
    void score(bool left_scored, double now);

    GameBoard board_;
    GameInput input_;
    KeyActivityProviderPtr provider_;

    bool running_{false};
    double last_time_{0.0};

    double ball_x_{0.0};
    double ball_y_{0.0};
    double ball_vx_{0.0};
    double ball_vy_{0.0};
    double ball_speed_{6.0};

    double left_y_{0.0};
    double right_y_{0.0};
    double paddle_height_{2.0};

    // Only used when the right-hand player is the computer.
    bool ai_opponent_{false};
    double ai_speed_{5.0};

    // Normalised key labels (see normalizeKeyLabel).
    std::string left_up_{"W"};
    std::string left_down_{"S"};
    std::string right_up_{"UP"};
    std::string right_down_{"DOWN"};

    int left_score_{0};
    int right_score_{0};
    int win_score_{7};

    double flash_until_{0.0};
    bool flash_is_left_{false};
    // A won game flashes longer and then resets the match.
    bool flash_is_win_{false};

    RgbColor color_left_{0, 200, 255};
    RgbColor color_right_{255, 90, 60};
    RgbColor color_ball_{255, 255, 255};
    RgbColor color_background_{0, 0, 0};
};

}  // namespace kb::cfg
