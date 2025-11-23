#include "keyboard_configurator/doom_fire_preset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kb::cfg {
namespace {
std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> out;
    std::string token;
    for (char ch : value) {
        if (ch == ',') {
            if (!token.empty()) {
                out.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) {
        out.push_back(token);
    }
    return out;
}
}

DoomFirePreset::DoomFirePreset()
    : rng_(std::random_device{}()),
      dist_(0.0, 1.0) {
    ensurePalette();
}

std::string DoomFirePreset::id() const { return "doom_fire"; }

void DoomFirePreset::configure(const ParameterMap& params) {
    auto parseDouble = [&](const char* key, double& target, double min_value = 0.0) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                double v = std::stod(it->second);
                if (min_value > 0.0) {
                    v = std::max(min_value, v);
                }
                target = v;
            } catch (...) {
            }
        }
    };

    parseDouble("speed", speed_, 0.01);
    parseDouble("cooling", cooling_, 0.0);
    parseDouble("spark_chance", spark_chance_, 0.0);
    parseDouble("spark_intensity", spark_intensity_, 0.0);
    parseDouble("step_interval", step_interval_, 0.001);

    if (auto it = params.find("palette"); it != params.end()) {
        palette_.clear();
        for (auto token : splitCommaList(it->second)) {
            // trim whitespace
            auto trim = [](std::string& s) {
                auto notSpace = [](int ch) { return !std::isspace(ch); };
                s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
                s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
            };
            trim(token);
            if (token.size() == 7 && token[0] == '#') {
                palette_.push_back(parseHexColor(token));
            }
        }
        ensurePalette();
    }
}

void DoomFirePreset::render(const KeyboardModel& model,
                            double time_seconds,
                            KeyColorFrame& frame) {
    const std::size_t key_count = model.keyCount();
    if (frame.size() != key_count) {
        frame.resize(key_count);
    }

    if (!grid_built_) {
        buildGrid(model);
    }
    ensurePalette();

    if (!grid_built_ || heat_.empty()) {
        frame.fill({0, 0, 0});
        return;
    }

    if (last_time_ == 0.0) {
        last_time_ = time_seconds;
    }
    double delta = time_seconds - last_time_;
    if (delta < 0.0) {
        delta = 0.0;
    }
    simulate(delta);
    last_time_ = time_seconds;

    for (std::size_t key = 0; key < key_count; ++key) {
        RgbColor color{0, 0, 0};
        if (key < key_to_cell_.size()) {
            int cell = key_to_cell_[key];
            if (cell >= 0 && cell < static_cast<int>(heat_.size())) {
                color = colorForHeat(heat_[cell]);
            }
        }
        frame.setColor(key, color);
    }
}

void DoomFirePreset::buildGrid(const KeyboardModel& model) {
    const auto& layout = model.layout();
    rows_ = layout.size();
    cols_ = 0;
    for (const auto& row : layout) {
        cols_ = std::max<std::size_t>(cols_, row.size());
    }
    if (rows_ == 0 || cols_ == 0) {
        grid_built_ = false;
        return;
    }

    const std::size_t cell_count = rows_ * cols_;
    cell_to_key_.assign(cell_count, -1);
    key_to_cell_.assign(model.keyCount(), -1);

    for (std::size_t r = 0; r < rows_; ++r) {
        const auto& row = layout[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            const auto& label = row[c];
            if (label == "NAN") {
                continue;
            }
            if (auto index = model.indexForKey(label)) {
                const std::size_t cell = r * cols_ + c;
                cell_to_key_[cell] = static_cast<int>(*index);
                key_to_cell_[*index] = static_cast<int>(cell);
            }
        }
    }

    heat_.assign(cell_count, 0.0);
    grid_built_ = true;
}

void DoomFirePreset::ensurePalette() {
    if (!palette_.empty()) {
        return;
    }
    constexpr const char* DEFAULT_PALETTE[] = {
        "#070707", "#1a0c02", "#330d03", "#661103", "#a71b04",
        "#d12402", "#f24f0f", "#f78d26", "#f7c35c", "#fff3a1"
    };
    for (const char* hex : DEFAULT_PALETTE) {
        palette_.push_back(parseHexColor(hex));
    }
}

void DoomFirePreset::simulate(double delta_seconds) {
    const double effective_speed = std::max(0.01, speed_);
    accumulator_ += delta_seconds * effective_speed;
    const double fixed_step = std::max(0.001, step_interval_);
    while (accumulator_ >= fixed_step) {
        igniteBaseRow();
        propagateFlames();
        accumulator_ -= fixed_step;
    }
}

void DoomFirePreset::igniteBaseRow() {
    if (!grid_built_ || rows_ == 0) {
        return;
    }
    const std::size_t base_row = rows_ - 1;
    const std::size_t offset = base_row * cols_;
    for (std::size_t c = 0; c < cols_; ++c) {
        const std::size_t cell = offset + c;
        if (cell >= heat_.size()) {
            continue;
        }
        if (cell_to_key_[cell] < 0) {
            continue;
        }
        double value = heat_[cell];
        value = std::max(0.0, value - cooling_ * (0.5 + 0.5 * dist_(rng_)));
        if (dist_(rng_) < spark_chance_) {
            value = spark_intensity_ * (0.6 + 0.4 * dist_(rng_));
        }
        heat_[cell] = std::clamp(value, 0.0, 1.0);
    }
}

void DoomFirePreset::propagateFlames() {
    if (!grid_built_ || rows_ < 2) {
        return;
    }

    for (std::size_t r = 0; r + 1 < rows_; ++r) {
        for (std::size_t c = 0; c < cols_; ++c) {
            const std::size_t dest_cell = r * cols_ + c;
            if (dest_cell >= heat_.size()) {
                continue;
            }
            if (cell_to_key_[dest_cell] < 0) {
                continue;
            }
            std::size_t src_col = c;
            const int shift = static_cast<int>(std::floor(dist_(rng_) * 3.0)) - 1; // -1,0,1
            const int shifted = static_cast<int>(src_col) + shift;
            if (shifted >= 0 && shifted < static_cast<int>(cols_)) {
                src_col = static_cast<std::size_t>(shifted);
            }
            const std::size_t src_cell = (r + 1) * cols_ + src_col;
            if (src_cell >= heat_.size()) {
                continue;
            }
            double h = heat_[src_cell];
            h = std::max(0.0, h - cooling_ * dist_(rng_));
            heat_[dest_cell] = std::clamp(h, 0.0, 1.0);
        }
    }
}

RgbColor DoomFirePreset::colorForHeat(double heat) const {
    if (palette_.empty()) {
        return {0, 0, 0};
    }
    heat = std::clamp(heat, 0.0, 1.0);
    const double pos = heat * (palette_.size() - 1);
    const std::size_t i0 = static_cast<std::size_t>(std::floor(pos));
    const std::size_t i1 = std::min(i0 + 1, palette_.size() - 1);
    const double t = pos - static_cast<double>(i0);
    const auto& a = palette_[i0];
    const auto& b = palette_[i1];
    auto lerp8 = [](std::uint8_t u, std::uint8_t v, double f) {
        return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(u + (v - u) * f)), 0, 255));
    };
    return {lerp8(a.r, b.r, t), lerp8(a.g, b.g, t), lerp8(a.b, b.b, t)};
}

RgbColor DoomFirePreset::parseHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        return {255, 255, 255};
    }
    auto hexValue = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (upper >= 'A' && upper <= 'F') return 10 + (upper - 'A');
        return 0;
    };
    auto hexComponent = [&](std::size_t idx) -> std::uint8_t {
        return static_cast<std::uint8_t>((hexValue(value[idx]) << 4) | hexValue(value[idx + 1]));
    };
    return {hexComponent(1), hexComponent(3), hexComponent(5)};
}

}  // namespace kb::cfg
