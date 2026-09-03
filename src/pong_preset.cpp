#include "keyboard_configurator/pong_preset.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

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

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

}  // namespace

void PongPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("ball_speed", ball_speed_, 0.5);
    number("ai_speed", ai_speed_, 0.5);
    number("paddle_height", paddle_height_, 1.0);

    auto color = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = parseHex(it->second, target);
        }
    };
    color("color_player", color_player_);
    color("color_ai", color_ai_);
    color("color_ball", color_ball_);
    color("background", color_background_);
}

void PongPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    provider_ = provider;
    input_.attach(std::move(provider));
}

void PongPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    running_ = true;
    player_score_ = 0;
    ai_score_ = 0;
    player_y_ = board_.height() / 2.0;
    ai_y_ = board_.height() / 2.0;
    last_time_ = 0.0;
    input_.reset();
    serve(1);
}

void PongPreset::stopGame() {
    running_ = false;
}

void PongPreset::serve(int towards) {
    ball_x_ = board_.width() / 2.0;
    ball_y_ = board_.height() / 2.0;

    // Serve at a shallow angle so the ball crosses the long axis rather than
    // pinging between the top and bottom rows.
    std::uniform_real_distribution<double> angle(-0.45, 0.45);
    const double a = angle(rng());
    ball_vx_ = std::cos(a) * ball_speed_ * (towards >= 0 ? 1.0 : -1.0);
    ball_vy_ = std::sin(a) * ball_speed_;
}

void PongPreset::handleInput(const KeyboardModel& model) {
    for (const auto& key : input_.poll(model)) {
        if (key == "UP" || key == "W") {
            player_y_ -= 1.0;
        } else if (key == "DOWN" || key == "S") {
            player_y_ += 1.0;
        }
    }
    const double limit = std::max(0.0, board_.height() - paddle_height_);
    player_y_ = std::clamp(player_y_, 0.0, limit);
}

void PongPreset::step(const KeyboardModel& model, double dt) {
    handleInput(model);

    const double height = board_.height();
    const double width = board_.width();

    ball_x_ += ball_vx_ * dt;
    ball_y_ += ball_vy_ * dt;

    // Bounce off the top and bottom rows.
    if (ball_y_ < 0.0) {
        ball_y_ = -ball_y_;
        ball_vy_ = -ball_vy_;
    } else if (ball_y_ > height - 1.0) {
        ball_y_ = 2.0 * (height - 1.0) - ball_y_;
        ball_vy_ = -ball_vy_;
    }

    // The computer chases the ball, but capped, so it can be beaten.
    const double target = ball_y_ - paddle_height_ / 2.0;
    const double step_limit = ai_speed_ * dt;
    ai_y_ += std::clamp(target - ai_y_, -step_limit, step_limit);
    ai_y_ = std::clamp(ai_y_, 0.0, std::max(0.0, height - paddle_height_));

    const auto hits = [&](double paddle_y) {
        return ball_y_ >= paddle_y - 0.5 && ball_y_ <= paddle_y + paddle_height_ - 0.5;
    };

    if (ball_x_ <= 0.0) {
        if (hits(player_y_)) {
            ball_x_ = -ball_x_;
            ball_vx_ = -ball_vx_;
            // Steer with the paddle: hitting off-centre angles the return.
            ball_vy_ += (ball_y_ - (player_y_ + paddle_height_ / 2.0 - 0.5)) * 2.0;
        } else {
            ++ai_score_;
            flash_until_ = last_time_ + 0.6;
            flash_is_player_ = false;
            serve(1);
        }
    } else if (ball_x_ >= width - 1.0) {
        if (hits(ai_y_)) {
            ball_x_ = 2.0 * (width - 1.0) - ball_x_;
            ball_vx_ = -ball_vx_;
            ball_vy_ += (ball_y_ - (ai_y_ + paddle_height_ / 2.0 - 0.5)) * 2.0;
        } else {
            ++player_score_;
            flash_until_ = last_time_ + 0.6;
            flash_is_player_ = true;
            serve(-1);
        }
    }

    // Keep the vertical component sane after repeated paddle english.
    ball_vy_ = std::clamp(ball_vy_, -ball_speed_, ball_speed_);
}

void PongPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_) {
        return;
    }
    if (board_.width() == 0) {
        board_.build(model);
    }

    const double dt = (last_time_ <= 0.0) ? 0.0 : std::min(0.1, time_seconds - last_time_);
    last_time_ = time_seconds;
    if (dt > 0.0) {
        step(model, dt);
    }

    const auto put = [&](int x, int y, RgbColor color) {
        if (auto index = board_.index(x, y)) {
            frame.setColor(*index, color);
        }
    };

    // A point scored flashes the scorer's whole side.
    const bool flashing = time_seconds < flash_until_;
    if (flashing) {
        const RgbColor tint = flash_is_player_ ? color_player_ : color_ai_;
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                put(x, y, mixColors(color_background_, tint, 0.25));
            }
        }
    }

    for (int i = 0; i < static_cast<int>(paddle_height_); ++i) {
        put(0, static_cast<int>(std::lround(player_y_)) + i, color_player_);
        put(board_.width() - 1, static_cast<int>(std::lround(ai_y_)) + i, color_ai_);
    }

    put(static_cast<int>(std::lround(ball_x_)), static_cast<int>(std::lround(ball_y_)), color_ball_);

    // Score shown as lit cells along the top row, from each player's own end.
    for (int i = 0; i < std::min(player_score_, board_.width() / 2 - 1); ++i) {
        put(1 + i, 0, mixColors(color_background_, color_player_, 0.7));
    }
    for (int i = 0; i < std::min(ai_score_, board_.width() / 2 - 1); ++i) {
        put(board_.width() - 2 - i, 0, mixColors(color_background_, color_ai_, 0.7));
    }
}

}  // namespace kb::cfg
