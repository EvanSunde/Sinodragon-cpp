#include "keyboard_configurator/reaction_preset.hpp"

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

void ReactionPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("min_wait", min_wait_, 0.1);
    number("max_wait", max_wait_, 0.2);
    number("result_seconds", result_seconds_, 0.2);
    number("slow_seconds", slow_seconds_, 0.05);

    auto colour = [&](const char* name, RgbColor& target) {
        if (auto it = params.find(name); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_target", color_target_);
    colour("color_fast", color_fast_);
    colour("color_slow", color_slow_);
    colour("color_false_start", color_false_start_);
    colour("background", color_background_);

    if (max_wait_ < min_wait_) {
        std::swap(min_wait_, max_wait_);
    }
}

void ReactionPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void ReactionPreset::arm(double now) {
    phase_ = Phase::Waiting;
    phase_started_ = now;
    // Random wait, so you cannot learn the rhythm and pre-empt it.
    lit_at_ = now + uniform(min_wait_, max_wait_);

    // Pick a playable cell for the target.
    for (int attempt = 0; attempt < 200; ++attempt) {
        const int x = std::uniform_int_distribution<int>(0, std::max(0, board_.width() - 1))(rng());
        const int y = std::uniform_int_distribution<int>(0, std::max(0, board_.height() - 1))(rng());
        if (auto index = board_.index(x, y)) {
            target_key_ = *index;
            target_x_ = x;
            target_y_ = y;
            return;
        }
    }
}

void ReactionPreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    input_.reset();
    last_seconds_ = 0.0;
    best_seconds_ = 0.0;
    running_ = true;
    arm(0.0);
}

void ReactionPreset::stopGame() {
    running_ = false;
}

void ReactionPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_) {
        return;
    }
    if (board_.width() == 0) {
        board_.build(model);
        arm(time_seconds);
    }
    if (phase_started_ == 0.0 && lit_at_ == 0.0) {
        arm(time_seconds);
    }

    const auto presses = input_.pollIndices(model);
    const auto put = [&](int x, int y, RgbColor c) {
        if (auto index = board_.index(x, y)) {
            frame.setColor(*index, c);
        }
    };

    switch (phase_) {
        case Phase::Waiting: {
            // Pressing anything before the light is a false start.
            if (!presses.empty()) {
                phase_ = Phase::FalseStart;
                phase_started_ = time_seconds;
                break;
            }
            if (time_seconds >= lit_at_) {
                phase_ = Phase::Lit;
                phase_started_ = time_seconds;
            }
            break;
        }
        case Phase::Lit: {
            put(target_x_, target_y_, color_target_);
            for (std::size_t key : presses) {
                if (key != target_key_) {
                    continue;  // wrong key: keep waiting for the right one
                }
                last_seconds_ = time_seconds - phase_started_;
                if (best_seconds_ <= 0.0 || last_seconds_ < best_seconds_) {
                    best_seconds_ = last_seconds_;
                }
                phase_ = Phase::Result;
                phase_started_ = time_seconds;
                break;
            }
            break;
        }
        case Phase::Result: {
            // Bar length is the time taken; colour runs green (fast) to red.
            const double ratio = std::clamp(last_seconds_ / slow_seconds_, 0.0, 1.0);
            const int cells = std::max(1, static_cast<int>(std::lround(ratio * board_.width())));
            const RgbColor tint = mixColors(color_fast_, color_slow_, ratio);
            for (int x = 0; x < std::min(cells, board_.width()); ++x) {
                put(x, board_.height() / 2, tint);
            }
            // Your best so far sits on the row above, dimmed.
            if (best_seconds_ > 0.0) {
                const double best_ratio = std::clamp(best_seconds_ / slow_seconds_, 0.0, 1.0);
                const int best_cells =
                    std::max(1, static_cast<int>(std::lround(best_ratio * board_.width())));
                for (int x = 0; x < std::min(best_cells, board_.width()); ++x) {
                    put(x, std::max(0, board_.height() / 2 - 1),
                        mixColors(color_background_, color_fast_, 0.35));
                }
            }
            if (time_seconds - phase_started_ > result_seconds_) {
                arm(time_seconds);
            }
            break;
        }
        case Phase::FalseStart: {
            const double pulse = 0.5 + 0.5 * std::sin(time_seconds * 16.0);
            for (int y = 0; y < board_.height(); ++y) {
                for (int x = 0; x < board_.width(); ++x) {
                    put(x, y, mixColors(color_background_, color_false_start_, pulse * 0.8));
                }
            }
            if (time_seconds - phase_started_ > result_seconds_) {
                arm(time_seconds);
            }
            break;
        }
    }
}

}  // namespace kb::cfg
