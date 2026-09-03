#include "keyboard_configurator/life_preset.hpp"

#include <algorithm>
#include <cctype>
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

void LifePreset::configure(const ParameterMap& params) {
    if (auto it = params.find("step_interval"); it != params.end()) {
        try {
            step_interval_ = std::max(0.02, std::stod(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("density"); it != params.end()) {
        try {
            density_ = std::clamp(std::stod(it->second), 0.0, 1.0);
        } catch (...) {
        }
    }
    auto color = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = parseHex(it->second, target);
        }
    };
    color("color_alive", color_alive_);
    color("color_new", color_new_);
    color("background", color_background_);
}

void LifePreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    input_.attach(std::move(provider));
}

std::size_t LifePreset::at(int x, int y) const {
    // Wrapping keeps patterns from dying against the edges of a 16x6 board.
    const int w = board_.width();
    const int h = board_.height();
    const int wrapped_x = ((x % w) + w) % w;
    const int wrapped_y = ((y % h) + h) % h;
    return static_cast<std::size_t>(wrapped_y) * w + wrapped_x;
}

void LifePreset::seed() {
    std::bernoulli_distribution alive(density_);
    for (int y = 0; y < board_.height(); ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            const std::size_t i = at(x, y);
            cells_[i] = board_.playable(x, y) && alive(rng()) ? 1 : 0;
            age_[i] = 0.0;
        }
    }
    generation_ = 0;
    stable_for_ = 0;
}

void LifePreset::startGame(const KeyboardModel& model) {
    board_.build(model);
    const std::size_t count = static_cast<std::size_t>(board_.width()) * board_.height();
    cells_.assign(count, 0);
    age_.assign(count, 0.0);
    scratch_.assign(count, 0);
    input_.reset();
    seed();
    running_ = true;
    last_step_ = 0.0;
}

void LifePreset::stopGame() {
    running_ = false;
}

int LifePreset::neighbours(int x, int y) const {
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            count += cells_[at(x + dx, y + dy)] ? 1 : 0;
        }
    }
    return count;
}

void LifePreset::step() {
    scratch_ = cells_;
    for (int y = 0; y < board_.height(); ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            const std::size_t i = at(x, y);
            if (!board_.playable(x, y)) {
                scratch_[i] = 0;
                continue;
            }
            const int n = neighbours(x, y);
            // B3/S23.
            scratch_[i] = cells_[i] ? ((n == 2 || n == 3) ? 1 : 0) : (n == 3 ? 1 : 0);
        }
    }

    const bool unchanged = scratch_ == cells_;
    cells_.swap(scratch_);

    for (std::size_t i = 0; i < cells_.size(); ++i) {
        age_[i] = cells_[i] ? std::min(1.0, age_[i] + 0.34) : 0.0;
    }

    ++generation_;

    // A small wrapped board settles into a still life or a blinker quickly.
    // Reseed rather than sit on a frozen keyboard.
    stable_for_ = unchanged ? stable_for_ + 1 : 0;
    const bool empty = std::none_of(cells_.begin(), cells_.end(), [](std::uint8_t c) { return c != 0; });
    if (stable_for_ >= 8 || empty) {
        seed();
    }
}

void LifePreset::applyInput(const KeyboardModel& model) {
    for (std::size_t key : input_.pollIndices(model)) {
        for (int y = 0; y < board_.height(); ++y) {
            for (int x = 0; x < board_.width(); ++x) {
                if (board_.index(x, y) == key) {
                    const std::size_t i = at(x, y);
                    cells_[i] = cells_[i] ? 0 : 1;
                    age_[i] = 0.0;
                    stable_for_ = 0;
                }
            }
        }
    }
}

void LifePreset::render(const KeyboardModel& model, double time_seconds, KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_background_);

    if (!running_ || cells_.empty()) {
        return;
    }

    applyInput(model);

    if (time_seconds - last_step_ >= step_interval_) {
        last_step_ = time_seconds;
        step();
    }

    for (int y = 0; y < board_.height(); ++y) {
        for (int x = 0; x < board_.width(); ++x) {
            const auto index = board_.index(x, y);
            if (!index) {
                continue;
            }
            const std::size_t i = at(x, y);
            if (!cells_[i]) {
                continue;
            }
            // Cells born this generation flash brighter, so the pattern reads
            // as moving rather than as a static blob.
            frame.setColor(*index, mixColors(color_new_, color_alive_, age_[i]));
        }
    }
}

}  // namespace kb::cfg
