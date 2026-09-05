#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Flappy on a sixteen-wide, six-tall board: the bird holds a fixed column near
// the left, walls scroll in from the right with a gap to fly through, and any
// key flaps. A wide short grid is exactly the shape this game wants.
class FlappyPreset : public GamePreset {
public:
    std::string id() const override { return "flappy"; }
    std::string gameName() const override { return "flappy"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    struct Wall {
        double x{0.0};
        int gap_top{0};  // first open row
        bool scored{false};
    };

    void restart();
    void step(const KeyboardModel& model, double dt, double now);

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    bool dead_{false};
    double died_at_{0.0};
    double last_time_{0.0};

    double bird_y_{0.0};
    double bird_vy_{0.0};
    int bird_x_{2};

    std::vector<Wall> walls_;
    double spawn_timer_{0.0};

    double gravity_{14.0};
    double flap_{-4.5};
    double scroll_{5.0};
    double spawn_interval_{1.4};
    int gap_size_{3};
    int score_{0};
    double restart_after_{2.5};

    RgbColor color_bird_{255, 230, 60};
    RgbColor color_wall_{40, 200, 90};
    RgbColor color_background_{0, 0, 10};
    RgbColor color_dead_{255, 40, 30};
};

}  // namespace kb::cfg
