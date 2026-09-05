#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Breakout on a six-row board: bricks fill the top rows, the paddle slides
// along the bottom row and the ball works upward. The wide short grid means the
// paddle moves left/right along sixteen columns, which is the natural shape.
class BreakoutPreset : public GamePreset {
public:
    std::string id() const override { return "breakout"; }
    std::string gameName() const override { return "breakout"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    void resetBall();
    void resetBricks();
    void step(const KeyboardModel& model, double dt);
    void handleInput(const KeyboardModel& model);
    [[nodiscard]] std::size_t brickAt(int x, int y) const {
        return static_cast<std::size_t>(y) * board_.width() + x;
    }

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    bool game_over_{false};
    bool cleared_{false};
    double ended_at_{0.0};
    double last_time_{0.0};

    std::vector<std::uint8_t> bricks_;  // 0 = gone, else hit points remaining
    int brick_rows_{2};

    double ball_x_{0.0};
    double ball_y_{0.0};
    double ball_vx_{0.0};
    double ball_vy_{0.0};
    double ball_speed_{7.0};

    double paddle_x_{0.0};
    double paddle_width_{3.0};
    int lives_{3};

    std::string left_key_{"LEFT"};
    std::string right_key_{"RIGHT"};
    std::string serve_key_{"SPACE"};
    bool waiting_to_serve_{true};

    std::vector<RgbColor> brick_palette_;
    RgbColor color_paddle_{80, 200, 255};
    RgbColor color_ball_{255, 255, 255};
    RgbColor color_background_{0, 0, 8};
    double restart_after_{4.0};
};

}  // namespace kb::cfg
