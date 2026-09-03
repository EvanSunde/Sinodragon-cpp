#include "keyboard_configurator/tetris_preset.hpp"

#include <algorithm>
#include <cctype>
#include <random>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

RgbColor parseHex(const std::string& value, RgbColor fallback) {
    if (value.size() != 7 || value.front() != '#') {
        return fallback;
    }
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return 0;
    };
    return {static_cast<std::uint8_t>((digit(value[1]) << 4) | digit(value[2])),
            static_cast<std::uint8_t>((digit(value[3]) << 4) | digit(value[4])),
            static_cast<std::uint8_t>((digit(value[5]) << 4) | digit(value[6]))};
}

// The seven tetrominoes, as cell offsets from their pivot.
const std::array<std::array<std::pair<int, int>, 4>, 7> kShapes = {{
    {{{0, 0}, {1, 0}, {2, 0}, {3, 0}}},   // I
    {{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},   // O
    {{{0, 0}, {1, 0}, {2, 0}, {1, 1}}},   // T
    {{{0, 0}, {1, 0}, {2, 0}, {2, 1}}},   // L
    {{{0, 1}, {1, 1}, {2, 1}, {2, 0}}},   // J
    {{{0, 1}, {1, 1}, {1, 0}, {2, 0}}},   // S
    {{{0, 0}, {1, 0}, {1, 1}, {2, 1}}},   // Z
}};

}  // namespace

void TetrisPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("step_interval"); it != params.end()) {
        try {
            step_interval_ = std::max(0.05, std::stod(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("background"); it != params.end()) {
        color_background_ = parseHex(it->second, color_background_);
    }
    if (auto it = params.find("palette"); it != params.end()) {
        std::vector<RgbColor> parsed;
        std::string token;
        const std::string& text = it->second;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == ',') {
                const auto begin = token.find_first_not_of(" \t");
                const auto end = token.find_last_not_of(" \t");
                if (begin != std::string::npos) {
                    const std::string hex = token.substr(begin, end - begin + 1);
                    if (hex.size() == 7 && hex[0] == '#') {
                        parsed.push_back(parseHex(hex, {255, 255, 255}));
                    }
                }
                token.clear();
            } else {
                token.push_back(text[i]);
            }
        }
        if (!parsed.empty()) {
            palette_ = std::move(parsed);
        }
    }
}

void TetrisPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

std::size_t TetrisPreset::at(int x, int y) const {
    return static_cast<std::size_t>(y) * board_.width() + x;
}

void TetrisPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    well_.assign(static_cast<std::size_t>(board_.width()) * board_.height(), 0);
    if (palette_.empty()) {
        palette_ = {{0, 240, 240}, {240, 240, 0},  {160, 0, 240}, {240, 160, 0},
                    {0, 0, 240},   {0, 240, 0},    {240, 0, 0}};
    }
    lines_ = 0;
    game_over_ = false;
    running_ = true;
    last_step_ = 0.0;
    input_.reset();
    spawn();
}

void TetrisPreset::stopGame() {
    running_ = false;
}

void TetrisPreset::spawn() {
    std::uniform_int_distribution<std::size_t> pick(0, kShapes.size() - 1);
    const std::size_t choice = pick(rng());
    for (int i = 0; i < 4; ++i) {
        // Shapes are authored upright; swap the axes so they enter lying down
        // along the direction of travel.
        piece_.dx[i] = kShapes[choice][i].first;
        piece_.dy[i] = kShapes[choice][i].second;
    }
    piece_.color_index = static_cast<int>(choice % palette_.size());

    piece_x_ = board_.width() - 4;
    piece_y_ = std::max(0, board_.height() / 2 - 1);

    if (collides(piece_, piece_x_, piece_y_)) {
        game_over_ = true;
    }
}

bool TetrisPreset::collides(const Piece& piece, int px, int py) const {
    for (int i = 0; i < 4; ++i) {
        const int x = px + piece.dx[i];
        const int y = py + piece.dy[i];
        if (x < 0 || y < 0 || x >= board_.width() || y >= board_.height()) {
            return true;
        }
        if (well_[at(x, y)] != 0) {
            return true;
        }
    }
    return false;
}

void TetrisPreset::lockPiece() {
    for (int i = 0; i < 4; ++i) {
        const int x = piece_x_ + piece_.dx[i];
        const int y = piece_y_ + piece_.dy[i];
        if (x >= 0 && y >= 0 && x < board_.width() && y < board_.height()) {
            well_[at(x, y)] = static_cast<std::uint8_t>(piece_.color_index + 1);
        }
    }
    lines_ += clearFullColumns();
    spawn();
}

int TetrisPreset::clearFullColumns() {
    int cleared = 0;
    // Gravity runs towards -x, so a completed *column* is the line to clear.
    for (int x = 0; x < board_.width(); ++x) {
        bool full = true;
        for (int y = 0; y < board_.height(); ++y) {
            if (well_[at(x, y)] == 0) {
                full = false;
                break;
            }
        }
        if (!full) {
            continue;
        }
        ++cleared;
        // Shift everything to the right of this column one step left.
        for (int sx = x; sx < board_.width() - 1; ++sx) {
            for (int y = 0; y < board_.height(); ++y) {
                well_[at(sx, y)] = well_[at(sx + 1, y)];
            }
        }
        for (int y = 0; y < board_.height(); ++y) {
            well_[at(board_.width() - 1, y)] = 0;
        }
        --x;  // re-test this column, it holds new contents now
    }
    return cleared;
}

void TetrisPreset::rotate() {
    Piece rotated = piece_;
    for (int i = 0; i < 4; ++i) {
        // 90 degrees about the pivot.
        const int dx = piece_.dx[i];
        const int dy = piece_.dy[i];
        rotated.dx[i] = -dy;
        rotated.dy[i] = dx;
    }

    // Normalise back to non-negative offsets so the piece keeps its anchor.
    int min_dx = 0;
    int min_dy = 0;
    for (int i = 0; i < 4; ++i) {
        min_dx = std::min(min_dx, rotated.dx[i]);
        min_dy = std::min(min_dy, rotated.dy[i]);
    }
    for (int i = 0; i < 4; ++i) {
        rotated.dx[i] -= min_dx;
        rotated.dy[i] -= min_dy;
    }

    // Simple wall kick: try in place, then nudged back towards the entry side.
    for (int kick : {0, 1, -1, 2}) {
        if (!collides(rotated, piece_x_ + kick, piece_y_)) {
            piece_ = rotated;
            piece_x_ += kick;
            return;
        }
    }
}

void TetrisPreset::handleInput(const KeyboardModel& model) {
    for (const auto& key : input_.poll(model)) {
        if (game_over_) {
            if (key == "ENTER" || key == "SPACE") {
                startGame(model);
            }
            continue;
        }
        if (key == "UP" || key == "W") {
            if (!collides(piece_, piece_x_, piece_y_ - 1)) --piece_y_;
        } else if (key == "DOWN" || key == "S") {
            if (!collides(piece_, piece_x_, piece_y_ + 1)) ++piece_y_;
        } else if (key == "SPACE" || key == "UP2") {
            rotate();
        } else if (key == "LEFT" || key == "A") {
            // Hard drop: slide as far along the travel direction as it goes.
            while (!collides(piece_, piece_x_ - 1, piece_y_)) {
                --piece_x_;
            }
            lockPiece();
        } else if (key == "RIGHT" || key == "D") {
            rotate();
        }
    }
}

void TetrisPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_ || well_.empty()) {
        return;
    }

    handleInput(model);

    if (game_over_) {
        // Pulse the stack red until the player restarts.
        const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 6.0);
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                if (auto index = board_.index(x, y)) {
                    frame.setColor(*index, mixColors(color_background_, RgbColor{255, 40, 30}, pulse));
                }
            }
        }
        return;
    }

    if (time_seconds - last_step_ >= step_interval_) {
        last_step_ = time_seconds;
        if (collides(piece_, piece_x_ - 1, piece_y_)) {
            lockPiece();
        } else {
            --piece_x_;
        }
    }

    for (int y = 0; y < board_.height(); ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            const std::uint8_t cell = well_[at(x, y)];
            if (cell == 0) {
                continue;
            }
            if (auto index = board_.index(x, y)) {
                frame.setColor(*index, palette_[(cell - 1) % palette_.size()]);
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (auto index = board_.index(piece_x_ + piece_.dx[i], piece_y_ + piece_.dy[i])) {
            frame.setColor(*index, palette_[piece_.color_index % palette_.size()]);
        }
    }
}

}  // namespace kb::cfg
