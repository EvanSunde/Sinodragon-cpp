#pragma once

#include <string>
#include <vector>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

// Connect Four, which is a 7x6 board -- and this keyboard is six rows tall, so
// it fits exactly, centred on the long axis with a column of margin either side.
//
// Two players share the keyboard: number keys 1-7 drop a disc into that column.
// The column under the current player's cursor pulses in their colour so you can
// see whose turn it is. Four in a row (any direction) flashes the winning line.
class ConnectFourPreset : public GamePreset {
public:
    std::string id() const override { return "connect4"; }
    std::string gameName() const override { return "connect4"; }

    void configure(const ParameterMap& params) override;
    void render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) override;
    [[nodiscard]] bool isAnimated() const noexcept override { return true; }
    void setKeyActivityProvider(KeyActivityProviderPtr provider) override;

    void startGame(const KeyboardModel& model) override;
    void stopGame() override;
    [[nodiscard]] bool isGameRunning() const override { return running_; }

private:
    static constexpr int kColumns = 7;
    static constexpr int kRows = 6;
    static constexpr int kConnect = 4;

    // 0 = empty, 1 = player one, 2 = player two.
    [[nodiscard]] int cell(int col, int row) const { return grid_[row * kColumns + col]; }
    void setCell(int col, int row, int who) { grid_[row * kColumns + col] = who; }

    bool drop(int column);
    [[nodiscard]] bool findWin(int col, int row, std::vector<std::pair<int, int>>& line) const;
    void handleInput(const KeyboardModel& model);
    void resetBoard();

    GameBoard board_;
    GameInput input_;

    bool running_{false};
    std::vector<int> grid_;
    int current_player_{1};
    int winner_{0};
    double win_at_{0.0};
    bool board_full_{false};

    // Where the 7x6 playfield sits on the physical grid.
    int origin_x_{0};
    int origin_y_{0};

    // Column keys, in order. Defaults to the number row.
    std::vector<std::string> column_keys_{"1", "2", "3", "4", "5", "6", "7"};
    std::string reset_key_{"ENTER"};

    RgbColor color_one_{255, 200, 0};
    RgbColor color_two_{255, 30, 60};
    RgbColor color_empty_{0, 0, 20};
    double restart_after_{6.0};
};

}  // namespace kb::cfg
