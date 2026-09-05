#include "keyboard_configurator/connect_four_preset.hpp"

#include <algorithm>
#include <cmath>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

void ConnectFourPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("column_keys"); it != params.end()) {
        auto keys = color::splitList(it->second);
        if (keys.size() >= kColumns) {
            column_keys_.assign(keys.begin(), keys.begin() + kColumns);
            for (auto& key : column_keys_) {
                key = normalizeKeyLabel(key);
            }
        }
    } else {
        for (auto& key : column_keys_) {
            key = normalizeKeyLabel(key);
        }
    }
    if (auto it = params.find("reset_key"); it != params.end()) {
        reset_key_ = normalizeKeyLabel(it->second);
    }
    if (auto it = params.find("restart_after"); it != params.end()) {
        try {
            restart_after_ = std::max(0.5, std::stod(it->second));
        } catch (...) {
        }
    }
    auto colour = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_one", color_one_);
    colour("color_two", color_two_);
    colour("color_empty", color_empty_);
}

void ConnectFourPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void ConnectFourPreset::resetBoard() {
    grid_.assign(kColumns * kRows, 0);
    current_player_ = 1;
    winner_ = 0;
    board_full_ = false;
    win_at_ = 0.0;
}

void ConnectFourPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    // Centre the 7x6 playfield; the board is wider than the game needs.
    origin_x_ = std::max(0, (board_.width() - kColumns) / 2);
    origin_y_ = std::max(0, (board_.height() - kRows) / 2);
    resetBoard();
    input_.reset();
    running_ = true;
}

void ConnectFourPreset::stopGame() {
    running_ = false;
}

bool ConnectFourPreset::drop(int column) {
    if (column < 0 || column >= kColumns) {
        return false;
    }
    // Discs fall to the lowest free row.
    for (int row = kRows - 1; row >= 0; --row) {
        if (cell(column, row) == 0) {
            setCell(column, row, current_player_);
            std::vector<std::pair<int, int>> line;
            if (findWin(column, row, line)) {
                winner_ = current_player_;
            }
            return true;
        }
    }
    return false;  // column full
}

bool ConnectFourPreset::findWin(int col, int row, std::vector<std::pair<int, int>>& line) const {
    const int who = cell(col, row);
    if (who == 0) {
        return false;
    }
    static const int kDir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (const auto& d : kDir) {
        line.clear();
        line.emplace_back(col, row);
        // Walk both ways along this axis counting matching discs.
        for (int sign = -1; sign <= 1; sign += 2) {
            int x = col + d[0] * sign;
            int y = row + d[1] * sign;
            while (x >= 0 && x < kColumns && y >= 0 && y < kRows && cell(x, y) == who) {
                line.emplace_back(x, y);
                x += d[0] * sign;
                y += d[1] * sign;
            }
        }
        if (static_cast<int>(line.size()) >= kConnect) {
            return true;
        }
    }
    line.clear();
    return false;
}

void ConnectFourPreset::handleInput(const KeyboardModel& model) {
    for (const auto& key : input_.poll(model)) {
        if (winner_ != 0 || board_full_) {
            if (key == reset_key_) {
                resetBoard();
            }
            continue;
        }
        for (int column = 0; column < kColumns; ++column) {
            if (key != column_keys_[static_cast<std::size_t>(column)]) {
                continue;
            }
            if (drop(column)) {
                if (winner_ == 0) {
                    board_full_ = std::none_of(grid_.begin(), grid_.end(),
                                               [](int v) { return v == 0; });
                    current_player_ = (current_player_ == 1) ? 2 : 1;
                }
            }
            break;
        }
    }
}

void ConnectFourPreset::render(const KeyboardModel& model, double time_seconds,
                               KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_empty_);

    if (!running_ || grid_.empty()) {
        return;
    }

    handleInput(model);

    if ((winner_ != 0 || board_full_) && win_at_ == 0.0) {
        win_at_ = time_seconds;
    }
    // Auto-restart so the board does not sit finished forever.
    if (win_at_ > 0.0 && time_seconds - win_at_ > restart_after_) {
        resetBoard();
    }

    const auto put = [&](int col, int row, RgbColor c) {
        if (auto index = board_.index(origin_x_ + col, origin_y_ + row)) {
            frame.setColor(*index, c);
        }
    };

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int who = cell(col, row);
            if (who == 1) {
                put(col, row, color_one_);
            } else if (who == 2) {
                put(col, row, color_two_);
            }
        }
    }

    if (winner_ != 0) {
        // Flash the winning line.
        std::vector<std::pair<int, int>> line;
        for (int row = 0; row < kRows && line.empty(); ++row) {
            for (int col = 0; col < kColumns; ++col) {
                if (cell(col, row) == winner_ && findWin(col, row, line)) {
                    break;
                }
            }
        }
        const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 10.0);
        const RgbColor win_color = (winner_ == 1) ? color_one_ : color_two_;
        for (const auto& [col, row] : line) {
            put(col, row, mixColors(color_empty_, win_color, pulse));
        }
        return;
    }

    if (board_full_) {
        return;
    }

    // Show whose turn it is: the next free slot of each column glows faintly,
    // brightest where a disc would actually land.
    const double pulse = 0.35 + 0.35 * std::sin(time_seconds * 4.0);
    const RgbColor turn = (current_player_ == 1) ? color_one_ : color_two_;
    for (int col = 0; col < kColumns; ++col) {
        for (int row = kRows - 1; row >= 0; --row) {
            if (cell(col, row) == 0) {
                put(col, row, mixColors(color_empty_, turn, pulse * 0.5));
                break;
            }
        }
    }
}

}  // namespace kb::cfg
