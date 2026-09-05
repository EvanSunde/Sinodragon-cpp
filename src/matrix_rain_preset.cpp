#include "keyboard_configurator/matrix_rain_preset.hpp"

#include <algorithm>
#include <cctype>
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

void MatrixRainPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("speed", speed_, 0.1);
    number("tail", tail_, 1.0);
    number("speed_variance", speed_variance_, 0.0);

    if (auto it = params.find("density"); it != params.end()) {
        try {
            density_ = std::clamp(std::stod(it->second), 0.0, 1.0);
        } catch (...) {
        }
    }
    if (auto it = params.find("direction"); it != params.end()) {
        std::string dir = it->second;
        std::transform(dir.begin(), dir.end(), dir.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        horizontal_ = (dir == "right" || dir == "left" || dir == "horizontal");
        // Recompute lanes: the travel axis changed.
        lanes_built_ = false;
    }

    if (auto it = params.find("color_head"); it != params.end()) {
        color_head_ = color::hex(it->second, color_head_);
    }
    if (auto it = params.find("color_tail"); it != params.end()) {
        color_tail_ = color::hex(it->second, color_tail_);
    }
    if (auto it = params.find("background"); it != params.end()) {
        color_background_ = color::hex(it->second, color_background_);
    }
}

void MatrixRainPreset::ensureLanes(const KeyboardModel& model) {
    if (lanes_built_ && board_.width() > 0) {
        return;
    }
    board_.build(model);

    // One drop per lane; a lane is a column (falling) or a row (ticker).
    const int lane_count = horizontal_ ? board_.height() : board_.width();
    const int travel = horizontal_ ? board_.width() : board_.height();

    lanes_.assign(static_cast<std::size_t>(std::max(0, lane_count)), Drop{});
    for (auto& drop : lanes_) {
        drop.active = uniform(0.0, 1.0) < density_;
        // Stagger the start so they do not all fall in lockstep.
        drop.position = uniform(-static_cast<double>(travel), static_cast<double>(travel));
        drop.speed = speed_ * uniform(1.0 - speed_variance_ * 0.5, 1.0 + speed_variance_ * 0.5);
        drop.length = std::max(1.0, tail_ * uniform(0.6, 1.4));
        drop.respawn_at = 0.0;
    }
    lanes_built_ = true;
}

void MatrixRainPreset::render(const KeyboardModel& model, double time_seconds,
                              KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    ensureLanes(model);
    if (lanes_.empty()) {
        return;
    }

    const double dt = (last_time_ < 0.0) ? 0.0 : std::clamp(time_seconds - last_time_, 0.0, 0.1);
    last_time_ = time_seconds;

    const int travel = horizontal_ ? board_.width() : board_.height();

    for (std::size_t lane = 0; lane < lanes_.size(); ++lane) {
        Drop& drop = lanes_[lane];

        if (!drop.active) {
            // Wait out the gap, then fall again from just off the top.
            if (time_seconds >= drop.respawn_at && uniform(0.0, 1.0) < density_) {
                drop.active = true;
                drop.position = -uniform(0.0, 2.0);
                drop.speed = speed_ * uniform(1.0 - speed_variance_ * 0.5, 1.0 + speed_variance_ * 0.5);
                drop.length = std::max(1.0, tail_ * uniform(0.6, 1.4));
            }
            continue;
        }

        drop.position += drop.speed * dt;
        if (drop.position - drop.length > travel) {
            drop.active = false;
            drop.respawn_at = time_seconds + uniform(0.1, 1.5);
            continue;
        }

        // Paint the head plus its fading tail.
        const int head = static_cast<int>(std::floor(drop.position));
        const int tail_cells = static_cast<int>(std::ceil(drop.length));
        for (int back = 0; back <= tail_cells; ++back) {
            const int cell = head - back;
            if (cell < 0 || cell >= travel) {
                continue;
            }
            const int x = horizontal_ ? cell : static_cast<int>(lane);
            const int y = horizontal_ ? static_cast<int>(lane) : cell;
            const auto index = board_.index(x, y);
            if (!index) {
                continue;
            }

            RgbColor color;
            if (back == 0) {
                color = color_head_;
            } else {
                const double fade = 1.0 - static_cast<double>(back) / (drop.length + 1.0);
                color = mixColors(color_background_, color_tail_, std::clamp(fade, 0.0, 1.0));
            }
            frame.setColor(*index, color);
        }
    }
}

}  // namespace kb::cfg
