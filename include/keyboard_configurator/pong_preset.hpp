#pragma once

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Pong across the width of the keyboard: your paddle is the leftmost column,
// the computer plays the rightmost. Up/Down move; the ball serves after each
// point. A short wide grid suits this far better than it suits a falling-block
// game.
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

    double player_y_{0.0};
    double ai_y_{0.0};
    double paddle_height_{2.0};
    double ai_speed_{5.0};

    int player_score_{0};
    int ai_score_{0};
    double flash_until_{0.0};
    bool flash_is_player_{false};

    RgbColor color_player_{0, 200, 255};
    RgbColor color_ai_{255, 90, 60};
    RgbColor color_ball_{255, 255, 255};
    RgbColor color_background_{0, 0, 0};
};

}  // namespace kb::cfg
