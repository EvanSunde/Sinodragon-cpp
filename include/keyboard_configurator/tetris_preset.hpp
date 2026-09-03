#pragma once

#include <array>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Tetris turned on its side.
//
// The board is six rows tall and sixteen columns wide, which is far too short
// for pieces to fall down it -- a piece would land almost immediately. So
// gravity runs along the long axis instead: pieces enter at the right and
// drift left, Up/Down move them across the six rows, Space rotates, and Left
// is the fast drop. A full *column* is what clears.
class TetrisPreset : public GamePreset {
public:
    std::string id() const override { return "tetris"; }
    std::string gameName() const override { return "tetris"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    // A piece is four cells, held as offsets so rotation is arithmetic.
    struct Piece {
        std::array<int, 4> dx{};
        std::array<int, 4> dy{};
        int color_index{0};
    };

    void spawn();
    bool collides(const Piece& piece, int px, int py) const;
    void lockPiece();
    int clearFullColumns();
    void rotate();
    void handleInput(const KeyboardModel& model);
    [[nodiscard]] std::size_t at(int x, int y) const;

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    bool game_over_{false};
    double game_over_at_{0.0};

    std::vector<std::uint8_t> well_;  // 0 = empty, else colour index + 1

    Piece piece_{};
    int piece_x_{0};
    int piece_y_{0};

    double step_interval_{0.45};
    double last_step_{0.0};
    int lines_{0};

    std::vector<RgbColor> palette_;
    RgbColor color_background_{0, 0, 0};
};

}  // namespace kb::cfg
