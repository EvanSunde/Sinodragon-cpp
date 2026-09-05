#include "keyboard_configurator/pong_preset.hpp"

#include <algorithm>
#include <cctype>
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

    if (auto it = params.find("win_score"); it != params.end()) {
        try {
            win_score_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }

    // Solo play against the computer stays available, it is just no longer the
    // default -- two people on one keyboard is the point of this one.
    if (auto it = params.find("opponent"); it != params.end()) {
        std::string mode = it->second;
        std::transform(mode.begin(), mode.end(), mode.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        ai_opponent_ = (mode == "ai" || mode == "cpu" || mode == "computer");
    }

    // Keys are compared against the same normalised labels GameInput reports,
    // so "up", "Up" and "KEY_UP" all mean the same thing in the config.
    auto key = [&](const char* name, std::string& target) {
        if (auto it = params.find(name); it != params.end() && !it->second.empty()) {
            target = normalizeKeyLabel(it->second);
        }
    };
    key("left_up", left_up_);
    key("left_down", left_down_);
    key("right_up", right_up_);
    key("right_down", right_down_);

    auto color = [&](const char* name, RgbColor& target) {
        if (auto it = params.find(name); it != params.end()) {
            target = parseHex(it->second, target);
        }
    };
    color("color_left", color_left_);
    color("color_right", color_right_);
    color("color_ball", color_ball_);
    color("background", color_background_);
    // Accept the old single-player names too, so existing configs keep working.
    color("color_player", color_left_);
    color("color_ai", color_right_);
}

void PongPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    provider_ = provider;
    input_.attach(std::move(provider));
}

void PongPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    running_ = true;
    left_score_ = 0;
    right_score_ = 0;
    left_y_ = board_.height() / 2.0;
    right_y_ = board_.height() / 2.0;
    last_time_ = 0.0;
    flash_until_ = 0.0;
    flash_is_win_ = false;
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
    const double limit = std::max(0.0, board_.height() - paddle_height_);

    for (const auto& key : input_.poll(model)) {
        if (key == left_up_) {
            left_y_ -= 1.0;
        } else if (key == left_down_) {
            left_y_ += 1.0;
        } else if (!ai_opponent_) {
            if (key == right_up_) {
                right_y_ -= 1.0;
            } else if (key == right_down_) {
                right_y_ += 1.0;
            }
        }
    }

    left_y_ = std::clamp(left_y_, 0.0, limit);
    right_y_ = std::clamp(right_y_, 0.0, limit);
}

void PongPreset::score(bool left_scored, double now) {
    if (left_scored) {
        ++left_score_;
    } else {
        ++right_score_;
    }

    flash_is_left_ = left_scored;
    const int winner_score = left_scored ? left_score_ : right_score_;

    if (winner_score >= win_score_) {
        // Match over: a longer flash in the winner's colour, then a fresh game.
        flash_is_win_ = true;
        flash_until_ = now + 2.0;
        left_score_ = 0;
        right_score_ = 0;
    } else {
        flash_is_win_ = false;
        flash_until_ = now + 0.6;
    }

    serve(left_scored ? 1 : -1);
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

    if (ai_opponent_) {
        // The computer chases the ball, but capped, so it can be beaten.
        const double target = ball_y_ - paddle_height_ / 2.0;
        const double step_limit = ai_speed_ * dt;
        right_y_ += std::clamp(target - right_y_, -step_limit, step_limit);
        right_y_ = std::clamp(right_y_, 0.0, std::max(0.0, height - paddle_height_));
    }

    const auto hits = [&](double paddle_y) {
        return ball_y_ >= paddle_y - 0.5 && ball_y_ <= paddle_y + paddle_height_ - 0.5;
    };

    if (ball_x_ <= 0.0) {
        if (hits(left_y_)) {
            ball_x_ = -ball_x_;
            ball_vx_ = -ball_vx_;
            // Steer with the paddle: hitting off-centre angles the return.
            ball_vy_ += (ball_y_ - (left_y_ + paddle_height_ / 2.0 - 0.5)) * 2.0;
        } else {
            score(/*left_scored=*/false, last_time_);
        }
    } else if (ball_x_ >= width - 1.0) {
        if (hits(right_y_)) {
            ball_x_ = 2.0 * (width - 1.0) - ball_x_;
            ball_vx_ = -ball_vx_;
            ball_vy_ += (ball_y_ - (right_y_ + paddle_height_ / 2.0 - 0.5)) * 2.0;
        } else {
            score(/*left_scored=*/true, last_time_);
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

    const bool flashing = time_seconds < flash_until_;
    if (flashing) {
        const RgbColor tint = flash_is_left_ ? color_left_ : color_right_;
        // Winning the match flashes harder than winning a rally.
        const double strength = flash_is_win_
                                    ? 0.35 + 0.45 * (0.5 + 0.5 * std::sin(time_seconds * 12.0))
                                    : 0.25;
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                put(x, y, mixColors(color_background_, tint, strength));
            }
        }
    }

    for (int i = 0; i < static_cast<int>(paddle_height_); ++i) {
        put(0, static_cast<int>(std::lround(left_y_)) + i, color_left_);
        put(board_.width() - 1, static_cast<int>(std::lround(right_y_)) + i, color_right_);
    }

    put(static_cast<int>(std::lround(ball_x_)), static_cast<int>(std::lround(ball_y_)), color_ball_);

    // Score along the top row, counting inward from each player's own end.
    const int half = std::max(1, board_.width() / 2 - 1);
    for (int i = 0; i < std::min(left_score_, half); ++i) {
        put(1 + i, 0, mixColors(color_background_, color_left_, 0.7));
    }
    for (int i = 0; i < std::min(right_score_, half); ++i) {
        put(board_.width() - 2 - i, 0, mixColors(color_background_, color_right_, 0.7));
    }
}

}  // namespace kb::cfg
