#include "keyboard_configurator/lightning_preset.hpp"

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

int pick(int low, int high) {
    return std::uniform_int_distribution<int>(low, high)(rng());
}

}  // namespace

void LightningPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("decay", decay_, 0.05);
    number("flicker", flicker_, 0.0);
    number("ambient_interval", ambient_interval_, 0.0);

    if (auto it = params.find("branch_chance"); it != params.end()) {
        try {
            branch_chance_ = std::clamp(std::stod(it->second), 0.0, 1.0);
        } catch (...) {
        }
    }
    if (auto it = params.find("max_length"); it != params.end()) {
        try {
            max_length_ = std::max(2, std::stoi(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("max_bolts"); it != params.end()) {
        try {
            max_bolts_ = std::max(1, std::stoi(it->second));
        } catch (...) {
        }
    }

    auto colour = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = color::hex(it->second, target);
        }
    };
    colour("color_core", color_core_);
    colour("color_glow", color_glow_);
    colour("background", color_background_);
}

void LightningPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void LightningPreset::strike(const KeyboardModel& model, std::size_t key_index, double now) {
    (void)model;

    // Find where the pressed key sits on the physical grid.
    int start_x = -1;
    int start_y = -1;
    for (int y = 0; y < board_.height() && start_x < 0; ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            if (board_.index(x, y) == key_index) {
                start_x = x;
                start_y = y;
                break;
            }
        }
    }
    if (start_x < 0) {
        return;
    }

    Bolt bolt;
    bolt.born = now;
    bolt.life = decay_ * uniform(0.8, 1.2);

    // Walk a jagged channel outward, occasionally spawning a shorter fork from
    // the current point. Direction is biased so a bolt travels rather than
    // wandering in circles.
    struct Walker {
        int x, y, dx, dy, budget;
        double intensity;
    };
    std::vector<Walker> walkers;
    const int dir = pick(0, 3);
    static const int kDx[4] = {1, -1, 0, 0};
    static const int kDy[4] = {0, 0, 1, -1};
    walkers.push_back({start_x, start_y, kDx[dir], kDy[dir], max_length_, 1.0});

    while (!walkers.empty()) {
        Walker w = walkers.back();
        walkers.pop_back();

        int x = w.x;
        int y = w.y;
        for (int step = 0; step < w.budget; ++step) {
            bolt.segments.push_back({x, y, w.intensity});

            // Mostly continue; sometimes jag sideways by one cell.
            if (uniform(0.0, 1.0) < 0.35) {
                if (w.dx != 0) {
                    y += pick(0, 1) ? 1 : -1;
                } else {
                    x += pick(0, 1) ? 1 : -1;
                }
            }
            x += w.dx;
            y += w.dy;

            if (x < 0 || y < 0 || x >= board_.width() || y >= board_.height()) {
                break;
            }

            // Forks are dimmer and shorter than the channel they leave.
            if (w.intensity > 0.5 && uniform(0.0, 1.0) < branch_chance_ &&
                walkers.size() < static_cast<std::size_t>(max_length_)) {
                const int fork = pick(0, 3);
                walkers.push_back({x, y, kDx[fork], kDy[fork],
                                   std::max(2, w.budget / 2), w.intensity * 0.55});
            }
        }
    }

    bolts_.push_back(std::move(bolt));
    if (bolts_.size() > static_cast<std::size_t>(max_bolts_)) {
        bolts_.erase(bolts_.begin());
    }
}

void LightningPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!board_built_) {
        board_.build(model);
        board_built_ = true;
    }

    for (std::size_t key : input_.pollIndices(model)) {
        strike(model, key, time_seconds);
    }

    if (ambient_interval_ > 0.0 && time_seconds >= next_ambient_) {
        next_ambient_ = time_seconds + ambient_interval_ * uniform(0.5, 1.5);
        const int x = pick(0, std::max(0, board_.width() - 1));
        const int y = pick(0, std::max(0, board_.height() - 1));
        if (auto index = board_.index(x, y)) {
            strike(model, *index, time_seconds);
        }
    }

    for (auto it = bolts_.begin(); it != bolts_.end();) {
        const double age = time_seconds - it->born;
        if (age >= it->life) {
            it = bolts_.erase(it);
            continue;
        }

        // Fade out, with a little flicker so it does not decay too smoothly.
        double strength = 1.0 - (age / it->life);
        strength *= 1.0 - flicker_ * (0.5 + 0.5 * std::sin(time_seconds * 60.0 + it->born * 13.0));
        strength = std::clamp(strength, 0.0, 1.0);

        for (const auto& segment : it->segments) {
            const auto index = board_.index(segment.x, segment.y);
            if (!index) {
                continue;
            }
            const double amount = std::clamp(strength * segment.intensity, 0.0, 1.0);
            // The channel core goes white, the falloff tints toward the glow.
            const RgbColor tint = mixColors(color_glow_, color_core_, segment.intensity);
            const RgbColor current = frame.color(*index);
            frame.setColor(*index, blendColors(current, mixColors(color_background_, tint, amount),
                                               BlendMode::Screen));
        }
        ++it;
    }
}

}  // namespace kb::cfg
