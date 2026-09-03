#pragma once

#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Conway's Game of Life on the physical key grid. Pressing a key toggles the
// cell under it, so the board is something you play with rather than watch.
// The grid wraps, which keeps gliders alive on a board this small.
class LifePreset : public GamePreset {
public:
    std::string id() const override { return "life"; }
    std::string gameName() const override { return "life"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    void seed();
    void step();
    void applyInput(const KeyboardModel& model);
    [[nodiscard]] int neighbours(int x, int y) const;
    [[nodiscard]] std::size_t at(int x, int y) const;

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    std::vector<std::uint8_t> cells_;
    std::vector<double> age_;   // frames alive, used to fade new cells in
    std::vector<std::uint8_t> scratch_;

    double step_interval_{0.25};
    double last_step_{0.0};
    double density_{0.28};
    int generation_{0};
    int stable_for_{0};

    RgbColor color_alive_{80, 255, 120};
    RgbColor color_new_{255, 255, 255};
    RgbColor color_background_{0, 8, 4};
};

}  // namespace kb::cfg
