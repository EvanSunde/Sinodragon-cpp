#include "keyboard_configurator/flappy_preset.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

int pick(int low, int high) {
    if (high < low) return low;
    return std::uniform_int_distribution<int>(low, high)(rng());
}

}  // namespace

void FlappyPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("gravity", gravity_, 0.1);
    number("scroll", scroll_, 0.1);
    number("spawn_interval", spawn_interval_, 0.2);
    number("restart_after", restart_after_, 0.2);

    if (auto it = params.find("flap"); it != params.end()) {
        try {
            // Stored negative (upward); accept either sign in the config.
            flap_ = -std::fabs(std::stod(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("gap"); it != params.end()) {
        try {
            gap_size_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }
    auto colour = [&](const char* name, RgbColor& target) {
        if (auto it = params.find(name); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_bird", color_bird_);
    colour("color_wall", color_wall_);
    colour("background", color_background_);
    colour("color_dead", color_dead_);
}

void FlappyPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void FlappyPreset::restart() {
    walls_.clear();
    bird_y_ = board_.height() / 2.0;
    bird_vy_ = 0.0;
    bird_x_ = std::min(2, std::max(0, board_.width() / 6));
    spawn_timer_ = 0.0;
    score_ = 0;
    dead_ = false;
    died_at_ = 0.0;
}

void FlappyPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    restart();
    input_.reset();
    last_time_ = 0.0;
    running_ = true;
}

void FlappyPreset::stopGame() {
    running_ = false;
}

void FlappyPreset::step(const KeyboardModel& model, double dt, double now) {
    // Any key flaps -- it is a one-button game, so do not make people hunt.
    const bool flapped = !input_.poll(model).empty();

    if (dead_) {
        if (flapped && now - died_at_ > 0.4) {
            restart();
        }
        return;
    }

    if (flapped) {
        bird_vy_ = flap_;
    }

    bird_vy_ += gravity_ * dt;
    bird_y_ += bird_vy_ * dt;

    // Floor and ceiling are fatal, as in the original.
    if (bird_y_ < 0.0 || bird_y_ > board_.height() - 1.0) {
        bird_y_ = std::clamp(bird_y_, 0.0, board_.height() - 1.0);
        dead_ = true;
        died_at_ = now;
        return;
    }

    spawn_timer_ -= dt;
    if (spawn_timer_ <= 0.0) {
        spawn_timer_ = spawn_interval_;
        Wall wall;
        wall.x = board_.width();
        const int gap = std::min(gap_size_, std::max(1, board_.height() - 1));
        wall.gap_top = pick(0, std::max(0, board_.height() - gap));
        walls_.push_back(wall);
    }

    const int bird_row = static_cast<int>(std::lround(bird_y_));
    for (auto it = walls_.begin(); it != walls_.end();) {
        it->x -= scroll_ * dt;

        const int wall_col = static_cast<int>(std::lround(it->x));
        if (wall_col == bird_x_) {
            const int gap = std::min(gap_size_, std::max(1, board_.height() - 1));
            const bool through_gap = bird_row >= it->gap_top && bird_row < it->gap_top + gap;
            if (!through_gap) {
                dead_ = true;
                died_at_ = now;
                return;
            }
            if (!it->scored) {
                it->scored = true;
                ++score_;
            }
        }

        if (it->x < -1.0) {
            it = walls_.erase(it);
        } else {
            ++it;
        }
    }
}

void FlappyPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_) {
        return;
    }
    if (board_.width() == 0) {
        board_.build(model);
        restart();
    }

    const double dt = (last_time_ <= 0.0) ? 0.0 : std::min(0.1, time_seconds - last_time_);
    last_time_ = time_seconds;
    if (dt > 0.0) {
        step(model, dt, time_seconds);
    }

    const auto put = [&](int x, int y, RgbColor c) {
        if (auto index = board_.index(x, y)) {
            frame.setColor(*index, c);
        }
    };

    if (dead_) {
        const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 9.0);
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                put(x, y, mixColors(color_background_, color_dead_, pulse * 0.7));
            }
        }
        // Score stays readable along the top while dead.
        for (int i = 0; i < std::min(score_, board_.width()); ++i) {
            put(i, 0, color_bird_);
        }
        if (time_seconds - died_at_ > restart_after_) {
            restart();
        }
        return;
    }

    const int gap = std::min(gap_size_, std::max(1, board_.height() - 1));
    for (const auto& wall : walls_) {
        const int col = static_cast<int>(std::lround(wall.x));
        if (col < 0 || col >= board_.width()) {
            continue;
        }
        for (int y = 0; y < board_.height(); ++y) {
            if (y < wall.gap_top || y >= wall.gap_top + gap) {
                put(col, y, color_wall_);
            }
        }
    }

    put(bird_x_, static_cast<int>(std::lround(bird_y_)), color_bird_);
}

}  // namespace kb::cfg
