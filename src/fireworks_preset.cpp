#include "keyboard_configurator/fireworks_preset.hpp"

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

constexpr double kTwoPi = 6.283185307179586;

}  // namespace

void FireworksPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("gravity", gravity_, 0.0);
    number("spark_life", spark_life_, 0.1);
    number("burst_speed", burst_speed_, 0.1);
    number("launch_speed", launch_speed_, 0.1);

    if (auto it = params.find("sparks"); it != params.end()) {
        try {
            sparks_per_burst_ = std::clamp(std::stoi(it->second), 1, 64);
        } catch (...) {
        }
    }
    if (auto it = params.find("launch_from_bottom"); it != params.end()) {
        std::string v = it->second;
        std::transform(v.begin(), v.end(), v.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        launch_from_bottom_ = (v == "1" || v == "true" || v == "yes" || v == "on");
    }
    if (auto it = params.find("palette"); it != params.end()) {
        auto parsed = color::palette(it->second);
        if (!parsed.empty()) {
            palette_ = std::move(parsed);
        }
    }
    if (auto it = params.find("background"); it != params.end()) {
        color_background_ = color::hex(it->second, color_background_);
    }
}

void FireworksPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

void FireworksPreset::launch(const KeyboardModel& model, std::size_t key_index) {
    (void)model;

    int target_x = -1;
    int target_y = -1;
    for (int y = 0; y < board_.height() && target_x < 0; ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            if (board_.index(x, y) == key_index) {
                target_x = x;
                target_y = y;
                break;
            }
        }
    }
    if (target_x < 0) {
        return;
    }

    if (palette_.empty()) {
        palette_ = {{255, 80, 60}, {255, 200, 60}, {80, 255, 120},
                    {80, 180, 255}, {220, 100, 255}, {255, 255, 255}};
    }
    const RgbColor color = palette_[static_cast<std::size_t>(uniform(0, static_cast<double>(
                                        palette_.size()))) % palette_.size()];

    if (!launch_from_bottom_) {
        // Burst immediately at the key, no rising shell.
        for (int i = 0; i < sparks_per_burst_; ++i) {
            const double angle = kTwoPi * i / sparks_per_burst_ + uniform(-0.2, 0.2);
            const double speed = burst_speed_ * uniform(0.5, 1.0);
            particles_.push_back({static_cast<double>(target_x), static_cast<double>(target_y),
                                  std::cos(angle) * speed, std::sin(angle) * speed,
                                  spark_life_ * uniform(0.7, 1.3), 0.0, color, false, 0.0});
        }
        return;
    }

    // A shell rising from the bottom row towards the pressed key.
    Particle shell;
    shell.x = target_x;
    shell.y = board_.height() - 1;
    shell.vx = 0.0;
    shell.vy = -launch_speed_;
    shell.life = 3.0;
    shell.color = color;
    shell.is_shell = true;
    shell.burst_y = target_y;
    particles_.push_back(shell);
}

void FireworksPreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!board_built_) {
        board_.build(model);
        board_built_ = true;
    }

    const double dt = (last_time_ < 0.0) ? 0.0 : std::clamp(time_seconds - last_time_, 0.0, 0.1);
    last_time_ = time_seconds;

    for (std::size_t key : input_.pollIndices(model)) {
        if (particles_.size() < max_particles_) {
            launch(model, key);
        }
    }

    std::vector<Particle> spawned;
    for (auto it = particles_.begin(); it != particles_.end();) {
        Particle& p = *it;
        p.age += dt;

        if (p.is_shell) {
            p.y += p.vy * dt;
            // Reached the pressed key: replace the shell with a ring of sparks.
            if (p.y <= p.burst_y || p.age >= p.life) {
                for (int i = 0; i < sparks_per_burst_; ++i) {
                    const double angle = kTwoPi * i / sparks_per_burst_ + uniform(-0.2, 0.2);
                    const double speed = burst_speed_ * uniform(0.5, 1.0);
                    spawned.push_back({p.x, p.burst_y, std::cos(angle) * speed,
                                       std::sin(angle) * speed, spark_life_ * uniform(0.7, 1.3),
                                       0.0, p.color, false, 0.0});
                }
                it = particles_.erase(it);
                continue;
            }
        } else {
            p.vy += gravity_ * dt;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.age >= p.life) {
                it = particles_.erase(it);
                continue;
            }
        }
        ++it;
    }

    for (auto& particle : spawned) {
        if (particles_.size() < max_particles_) {
            particles_.push_back(particle);
        }
    }

    for (const auto& p : particles_) {
        const auto index = board_.index(static_cast<int>(std::lround(p.x)),
                                        static_cast<int>(std::lround(p.y)));
        if (!index) {
            continue;
        }
        const double fade = p.is_shell ? 1.0 : std::clamp(1.0 - p.age / p.life, 0.0, 1.0);
        // Additive, so overlapping sparks brighten instead of replacing.
        const RgbColor current = frame.color(*index);
        frame.setColor(*index,
                       blendColors(current, mixColors(color_background_, p.color, fade),
                                   BlendMode::Add));
    }
}

}  // namespace kb::cfg
