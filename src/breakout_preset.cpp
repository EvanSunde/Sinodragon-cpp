#include "keyboard_configurator/breakout_preset.hpp"

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

double uniform(double low, double high) {
    return std::uniform_real_distribution<double>(low, high)(rng());
}

}  // namespace

void BreakoutPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("ball_speed", ball_speed_, 0.5);
    number("paddle_width", paddle_width_, 1.0);
    number("restart_after", restart_after_, 0.5);

    if (auto it = params.find("brick_rows"); it != params.end()) {
        try {
            brick_rows_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("lives"); it != params.end()) {
        try {
            lives_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }
    auto key = [&](const char* name, std::string& target) {
        if (auto it = params.find(name); it != params.end() && !it->second.empty()) {
            target = normalizeKeyLabel(it->second);
        }
    };
    key("left_key", left_key_);
    key("right_key", right_key_);
    key("serve_key", serve_key_);

    if (auto it = params.find("brick_palette"); it != params.end()) {
        auto parsed = color::palette(it->second);
        if (!parsed.empty()) {
            brick_palette_ = std::move(parsed);
        }
    }
    auto colour = [&](const char* name, RgbColor& target) {
        if (auto it = params.find(name); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_paddle", color_paddle_);
    colour("color_ball", color_ball_);
    colour("background", color_background_);
}

void BreakoutPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void BreakoutPreset::resetBricks() {
    bricks_.assign(static_cast<std::size_t>(board_.width()) * board_.height(), 0);
    const int rows = std::min(brick_rows_, std::max(0, board_.height() - 2));
    for (int y = 0; y < rows; ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            if (board_.playable(x, y)) {
                bricks_[brickAt(x, y)] = 1;
            }
        }
    }
    cleared_ = false;
}

void BreakoutPreset::resetBall() {
    paddle_x_ = (board_.width() - paddle_width_) / 2.0;
    ball_x_ = paddle_x_ + paddle_width_ / 2.0;
    ball_y_ = board_.height() - 2.0;
    // Serve upward at a slight angle so it does not bounce straight up and down.
    const double angle = uniform(-0.5, 0.5);
    ball_vx_ = std::sin(angle) * ball_speed_;
    ball_vy_ = -std::cos(angle) * ball_speed_;
    waiting_to_serve_ = true;
}

void BreakoutPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    if (brick_palette_.empty()) {
        brick_palette_ = {{255, 70, 70}, {255, 170, 40}, {255, 240, 60},
                          {90, 230, 120}, {90, 170, 255}};
    }
    resetBricks();
    resetBall();
    game_over_ = false;
    ended_at_ = 0.0;
    last_time_ = 0.0;
    input_.reset();
    running_ = true;
}

void BreakoutPreset::stopGame() {
    running_ = false;
}

void BreakoutPreset::handleInput(const KeyboardModel& model) {
    for (const auto& key : input_.poll(model)) {
        if (game_over_ || cleared_) {
            if (key == serve_key_) {
                resetBricks();
                resetBall();
                game_over_ = false;
                ended_at_ = 0.0;
            }
            continue;
        }
        if (key == left_key_) {
            paddle_x_ -= 1.0;
        } else if (key == right_key_) {
            paddle_x_ += 1.0;
        } else if (key == serve_key_) {
            waiting_to_serve_ = false;
        }
    }
    paddle_x_ = std::clamp(paddle_x_, 0.0, std::max(0.0, board_.width() - paddle_width_));
}

void BreakoutPreset::step(const KeyboardModel& model, double dt) {
    handleInput(model);

    if (waiting_to_serve_) {
        // The ball rides the paddle until served.
        ball_x_ = paddle_x_ + paddle_width_ / 2.0;
        ball_y_ = board_.height() - 2.0;
        return;
    }

    const double width = board_.width();
    const double height = board_.height();

    ball_x_ += ball_vx_ * dt;
    ball_y_ += ball_vy_ * dt;

    // Side walls.
    if (ball_x_ < 0.0) {
        ball_x_ = -ball_x_;
        ball_vx_ = -ball_vx_;
    } else if (ball_x_ > width - 1.0) {
        ball_x_ = 2.0 * (width - 1.0) - ball_x_;
        ball_vx_ = -ball_vx_;
    }
    // Ceiling.
    if (ball_y_ < 0.0) {
        ball_y_ = -ball_y_;
        ball_vy_ = -ball_vy_;
    }

    // Brick collision at the ball's cell.
    const int bx = static_cast<int>(std::lround(ball_x_));
    const int by = static_cast<int>(std::lround(ball_y_));
    if (bx >= 0 && by >= 0 && bx < board_.width() && by < board_.height()) {
        auto& brick = bricks_[brickAt(bx, by)];
        if (brick > 0) {
            brick = 0;
            ball_vy_ = -ball_vy_;
            cleared_ = std::none_of(bricks_.begin(), bricks_.end(),
                                    [](std::uint8_t b) { return b != 0; });
        }
    }

    // Paddle sits on the bottom row.
    const double paddle_row = height - 1.0;
    if (ball_y_ >= paddle_row - 0.5) {
        if (ball_x_ >= paddle_x_ - 0.5 && ball_x_ <= paddle_x_ + paddle_width_ - 0.5) {
            ball_y_ = paddle_row - 0.5;
            ball_vy_ = -std::fabs(ball_vy_);
            // Hitting off-centre angles the bounce, as it should.
            const double offset = (ball_x_ - (paddle_x_ + paddle_width_ / 2.0 - 0.5)) /
                                  std::max(1.0, paddle_width_ / 2.0);
            ball_vx_ += offset * ball_speed_ * 0.6;
            ball_vx_ = std::clamp(ball_vx_, -ball_speed_, ball_speed_);
        } else if (ball_y_ > height) {
            if (--lives_ <= 0) {
                game_over_ = true;
            } else {
                resetBall();
            }
        }
    }
}

void BreakoutPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_ || bricks_.empty()) {
        return;
    }

    const double dt = (last_time_ <= 0.0) ? 0.0 : std::min(0.1, time_seconds - last_time_);
    last_time_ = time_seconds;

    const auto put = [&](int x, int y, RgbColor c) {
        if (auto index = board_.index(x, y)) {
            frame.setColor(*index, c);
        }
    };

    if (game_over_ || cleared_) {
        if (ended_at_ == 0.0) {
            ended_at_ = time_seconds;
        }
        handleInput(model);
        if (time_seconds - ended_at_ > restart_after_) {
            lives_ = std::max(1, lives_);
            resetBricks();
            resetBall();
            game_over_ = false;
            ended_at_ = 0.0;
        }
        // Win pulses green across the board, loss pulses red.
        const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 8.0);
        const RgbColor tint = cleared_ ? RgbColor{60, 255, 90} : RgbColor{255, 50, 40};
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                put(x, y, mixColors(color_background_, tint, pulse * 0.8));
            }
        }
        return;
    }

    if (dt > 0.0) {
        step(model, dt);
    }

    for (int y = 0; y < board_.height(); ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            if (bricks_[brickAt(x, y)] != 0) {
                put(x, y, brick_palette_[static_cast<std::size_t>(y) % brick_palette_.size()]);
            }
        }
    }

    for (int i = 0; i < static_cast<int>(paddle_width_); ++i) {
        put(static_cast<int>(std::lround(paddle_x_)) + i, board_.height() - 1, color_paddle_);
    }

    put(static_cast<int>(std::lround(ball_x_)), static_cast<int>(std::lround(ball_y_)), color_ball_);
}

}  // namespace kb::cfg
