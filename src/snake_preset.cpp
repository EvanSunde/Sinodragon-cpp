#include "keyboard_configurator/snake_preset.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <linux/input-event-codes.h>

namespace kb::cfg {

namespace {
    std::mt19937& rng()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        return gen;
    }

    std::string toUpper(std::string str)
    {
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return str;
    }
}

std::string SnakePreset::id() const { return "snake"; }

void SnakePreset::configure(const ParameterMap& params)
{
    if (params.count("step_interval")) {
        try { step_interval_ = std::stod(params.at("step_interval")); } catch(...) {}
    }
}

void SnakePreset::setKeyActivityProvider(KeyActivityProviderPtr provider)
{
    key_activity_provider_ = provider;
}

void SnakePreset::start(const KeyboardModel& model)
{
    is_running_ = true;
    reset(model);
}

void SnakePreset::stop()
{
    is_running_ = false;
}

void SnakePreset::reset(const KeyboardModel& model)
{
    snake_.clear();
    const auto& layout = model.layout();
    if (layout.empty()) return;

    // Find a valid starting cell closest to the center so we never land on a NAN gap.
    Position start{0, 0};
    bool found_start = false;
    const int target_r = static_cast<int>(layout.size() / 2);
    const int target_c = layout[target_r].empty() ? 0 : static_cast<int>(layout[target_r].size() / 2);
    int best_score = std::numeric_limits<int>::max();

    for (int r = 0; r < static_cast<int>(layout.size()); ++r) {
        for (int c = 0; c < static_cast<int>(layout[r].size()); ++c) {
            if (layout[r][c] == "NAN") continue;
            int score = std::abs(r - target_r) + std::abs(c - target_c);
            if (score < best_score) {
                best_score = score;
                start = { r, c };
                found_start = true;
            }
        }
    }

    if (!found_start) return;

    snake_.push_back(start);
    current_dir_ = Direction::RIGHT;
    game_over_ = false;
    input_queue_.clear();
    
    spawnFood(model);
    
    internal_time_ = 0.0;
    last_step_time_ = 0.0;
    last_real_time_ = 0.0;
    last_input_time_ = 0.0;
    if (key_activity_provider_) {
        last_input_time_ = key_activity_provider_->nowSeconds();
    }
}

void SnakePreset::spawnFood(const KeyboardModel& model)
{
    const auto& layout = model.layout();
    std::vector<Position> valid_positions;
    
    for (int r = 0; r < static_cast<int>(layout.size()); ++r) {
        for (int c = 0; c < static_cast<int>(layout[r].size()); ++c) {
            if (layout[r][c] != "NAN") {
                Position p{r, c};
                if (std::find(snake_.begin(), snake_.end(), p) == snake_.end()) {
                    valid_positions.push_back(p);
                }
            }
        }
    }
    
    if (!valid_positions.empty()) {
        std::uniform_int_distribution<size_t> dist(0, valid_positions.size() - 1);
        food_ = valid_positions[dist(rng())];
    }
}

void SnakePreset::processInput(const KeyboardModel& model)
{
    if (!key_activity_provider_) return;

    double now = key_activity_provider_->nowSeconds();
    auto events = key_activity_provider_->recentEvents(now - last_input_time_);
    last_input_time_ = now;

    if (game_over_) {
        for (const auto& ev : events) {
            if (ev.key_index >= model.keyCount()) continue;
            const std::string& label = model.keyLabels()[ev.key_index];
            const std::string upper = toUpper(label);
            if (upper == "KEY_ENTER" || upper == "ENTER" || upper == "RETURN" ||
                upper == "KEY_SPACE" || upper == "SPACE" || upper == "SPACEBAR" ||
                upper == "KEY_ESC" || upper == "ESC" || upper == "ESCAPE") {
                reset(model);
                return;
            }
        }
        return;
    }

    for (const auto& ev : events) {
        if (ev.key_index >= model.keyCount()) continue;
        
        // Find which key was pressed
        const auto& labels = model.keyLabels();
        const std::string& label = labels[ev.key_index];
        const std::string upper = toUpper(label);
        
        Direction attempted_dir = current_dir_;
        bool is_dir_key = true;
        if (upper == "KEY_UP" || upper == "UP" || upper == "ARROWUP") attempted_dir = Direction::UP;
        else if (upper == "KEY_DOWN" || upper == "DOWN" || upper == "ARROWDOWN") attempted_dir = Direction::DOWN;
        else if (upper == "KEY_LEFT" || upper == "LEFT" || upper == "ARROWLEFT") attempted_dir = Direction::LEFT;
        else if (upper == "KEY_RIGHT" || upper == "RIGHT" || upper == "ARROWRIGHT") attempted_dir = Direction::RIGHT;
        else is_dir_key = false;

        if (is_dir_key) {
            Direction last_queued = input_queue_.empty() ? current_dir_ : input_queue_.back();
            bool is_opposite = false;
            if (attempted_dir == Direction::UP && last_queued == Direction::DOWN) is_opposite = true;
            if (attempted_dir == Direction::DOWN && last_queued == Direction::UP) is_opposite = true;
            if (attempted_dir == Direction::LEFT && last_queued == Direction::RIGHT) is_opposite = true;
            if (attempted_dir == Direction::RIGHT && last_queued == Direction::LEFT) is_opposite = true;

            if (!is_opposite && input_queue_.size() < 3) {
                input_queue_.push_back(attempted_dir);
            }
        }
    }
}

namespace {
RgbColor hsvToRgb(double h, double s, double v)
{
    h = std::fmod(h, 360.0);
    if (h < 0) h += 360.0;
    s = std::clamp(s, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);

    double c = v * s;
    double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2) - 1));
    double m = v - c;

    double r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }

    return {
        static_cast<std::uint8_t>((r + m) * 255),
        static_cast<std::uint8_t>((g + m) * 255),
        static_cast<std::uint8_t>((b + m) * 255)
    };
}
}

void SnakePreset::randomizeColors()
{
    std::uniform_real_distribution<double> hue_dist(0.0, 360.0);
    std::uniform_real_distribution<double> sat_dist(0.7, 1.0);
    std::uniform_real_distribution<double> val_dist(0.6, 1.0); // Make colors brighter. for low brightness use 0.2-0.7

    auto generateColor = [&]() {
        double hue = hue_dist(rng());
        return hsvToRgb(hue, sat_dist(rng()), val_dist(rng()));
    };

    auto isCloseToFood = [&](const RgbColor& c) {
        int dr = static_cast<int>(c.r) - static_cast<int>(color_food_.r);
        int dg = static_cast<int>(c.g) - static_cast<int>(color_food_.g);
        int db = static_cast<int>(c.b) - static_cast<int>(color_food_.b);
        int dist_sq = dr * dr + dg * dg + db * db;
        return dist_sq < 2000;
    };

    do {
        color_head_ = generateColor();
    } while (isCloseToFood(color_head_));

    do {
        color_body_ = generateColor();
    } while (isCloseToFood(color_body_));
}

bool SnakePreset::isValidPosition(const KeyboardModel& model, const Position& pos) const
{
    const auto& layout = model.layout();
    if (pos.r < 0 || pos.r >= static_cast<int>(layout.size())) return false;
    if (pos.c < 0 || pos.c >= static_cast<int>(layout[pos.r].size())) return false;
    return layout[pos.r][pos.c] != "NAN";
}

std::optional<SnakePreset::Position> SnakePreset::advanceWrapped(const KeyboardModel& model,
                                                                 const Position& from,
                                                                 Direction dir) const
{
    const auto& layout = model.layout();
    if (layout.empty()) return std::nullopt;

    auto wrapRow = [&](int r) {
        const int rows = static_cast<int>(layout.size());
        if (rows == 0) return 0;
        while (r < 0) r += rows;
        while (r >= rows) r -= rows;
        return r;
    };

    auto wrapCol = [&](int row, int c) -> std::optional<int> {
        if (row < 0 || row >= static_cast<int>(layout.size())) return std::nullopt;
        const int cols = static_cast<int>(layout[row].size());
        if (cols == 0) return std::nullopt;
        while (c < 0) c += cols;
        while (c >= cols) c -= cols;
        return c;
    };

    auto validCell = [&](const Position& p) {
        return p.r >= 0 && p.r < static_cast<int>(layout.size()) &&
            p.c >= 0 && p.c < static_cast<int>(layout[p.r].size()) &&
            layout[p.r][p.c] != "NAN";
    };

    Position candidate = from;
    const int max_attempts = static_cast<int>(layout.size()) * 8;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        switch (dir) {
        case Direction::UP:
        case Direction::DOWN: {
            int delta = (dir == Direction::UP) ? -1 : 1;
            auto col = wrapCol(candidate.r, candidate.c + delta);
            if (!col) continue;
            candidate.c = *col;
            break;
        }
        case Direction::LEFT:
            candidate.r = wrapRow(candidate.r - 1);
            break;
        case Direction::RIGHT:
            candidate.r = wrapRow(candidate.r + 1);
            break;
        }

        if (dir == Direction::LEFT || dir == Direction::RIGHT) {
            auto col = wrapCol(candidate.r, candidate.c);
            if (!col) continue;
            candidate.c = *col;
        }

        if (validCell(candidate)) {
            return candidate;
        }
    }

    return std::nullopt;
}

void SnakePreset::updateGame(const KeyboardModel& model)
{
    if (game_over_ || snake_.empty()) return;

    if (!input_queue_.empty()) {
        current_dir_ = input_queue_.front();
        input_queue_.pop_front();
    }

    auto next_pos = advanceWrapped(model, snake_.front(), current_dir_);
    if (!next_pos) {
        return;
    }

    Position next_head = *next_pos;

    bool ate_food = (next_head == food_);
    Position removed_tail;
    bool tail_removed = false;
    if (!ate_food && !snake_.empty()) {
        removed_tail = snake_.back();
        snake_.pop_back();
        tail_removed = true;
    }

    if (std::find(snake_.begin(), snake_.end(), next_head) != snake_.end()) {
        game_over_ = true;
        if (tail_removed) {
            snake_.push_back(removed_tail);
        }
        return;
    }

    snake_.push_front(next_head);

    if (ate_food) {
        spawnFood(model);
        randomizeColors();
    }
}

void SnakePreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame)
{
    if (!is_running_) return;

    double dt = (last_real_time_ > 0) ? (time_seconds - last_real_time_) : 0.0;
    last_real_time_ = time_seconds;
    if (dt < 0) dt = 0;
    if (dt > 0.5) dt = 0; // Pause threshold

    internal_time_ += dt;

    processInput(model);

    if (internal_time_ - last_step_time_ >= step_interval_) {
        updateGame(model);
        last_step_time_ = internal_time_;
    }

    const auto& layout = model.layout();
    size_t key_idx = 0;
    
    for (size_t r = 0; r < layout.size(); ++r) {
        for (size_t c = 0; c < layout[r].size(); ++c) {
            if (layout[r][c] != "NAN") {
                Position p{static_cast<int>(r), static_cast<int>(c)};
                RgbColor color = {0, 0, 0};
                
                if (game_over_) {
                    if (std::find(snake_.begin(), snake_.end(), p) != snake_.end()) {
                        color = color_dead_;
                    }
                } else {
                    if (!snake_.empty() && p == snake_.front()) {
                        color = color_head_;
                    } else if (std::find(snake_.begin(), snake_.end(), p) != snake_.end()) {
                        color = color_body_;
                    } else if (p == food_) {
                        // Blink food
                        double pulse = (std::sin(internal_time_ * 10.0) + 1.0) / 2.0;
                        color = {
                            static_cast<uint8_t>(color_food_.r * pulse),
                            static_cast<uint8_t>(color_food_.g * pulse),
                            static_cast<uint8_t>(color_food_.b * pulse)
                        };
                    }
                }
                
                if (key_idx < frame.size()) {
                    frame.setColor(key_idx, color);
                }
            }
            key_idx++;
        }
    }
}

} // namespace kb::cfg
