#include "keyboard_configurator/reactive_ripple_preset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kb::cfg {
namespace {
std::string trim(const std::string& input) {
    auto begin = std::find_if_not(input.begin(), input.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    auto end = std::find_if_not(input.rbegin(), input.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : value) {
        if (ch == ',') {
            if (!current.empty()) {
                tokens.push_back(trim(current));
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(trim(current));
    }
    return tokens;
}
}  // namespace

void ReactiveRipplePreset::configure(const ParameterMap& params) {
    auto parseDouble = [&](const char* key, double& target, double min_val = 0.0) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                double v = std::stod(it->second);
                if (min_val > 0.0) v = std::max(min_val, v);
                target = v;
            } catch (...) {
            }
        }
    };

    parseDouble("wave_speed", wave_speed_, 0.1);
    parseDouble("decay_time", decay_time_, 0.05);
    parseDouble("thickness", thickness_, 0.01);
    parseDouble("history", history_window_, 0.1);
    parseDouble("intensity", intensity_scale_, 0.0);

    if (auto it = params.find("color"); it != params.end()) {
        ripple_color_ = parseHexColor(it->second);
    }
    if (auto it = params.find("base_color"); it != params.end()) {
        base_color_ = parseHexColor(it->second);
    }
}

void ReactiveRipplePreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    provider_ = std::move(provider);
}

void ReactiveRipplePreset::render(const KeyboardModel& model,
                                  double /*time_seconds*/,
                                  KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) {
        frame.resize(total);
    }
    if (!coords_built_) {
        buildCoords(model);
    }

    frame.fill(base_color_);

    if (!provider_) {
        return;
    }

    if (xs_.size() != total || ys_.size() != total) {
        coords_built_ = false;
        buildCoords(model);
    }

    const double thickness = std::max(0.005, thickness_);
    const double decay = std::max(0.01, decay_time_);
    const double speed = std::max(0.01, wave_speed_);

    const auto events = provider_->recentEvents(history_window_);
    if (events.empty()) {
        return;
    }

    std::vector<double> contributions(total, 0.0);
    const double now = provider_->nowSeconds();
    for (const auto& ev : events) {
        if (ev.key_index >= xs_.size()) {
            continue;
        }
        const double ex = xs_[ev.key_index];
        const double ey = ys_[ev.key_index];
        const double age = std::max(0.0, now - ev.time_seconds);
        const double radius = speed * age;
        if (radius <= 0.0) {
            continue;
        }
        const double decay_factor = std::exp(-age / decay);
        for (std::size_t k = 0; k < total; ++k) {
            const double dx = xs_[k] - ex;
            const double dy = ys_[k] - ey;
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double diff = std::abs(dist - radius);
            if (diff > thickness) {
                continue;
            }
            double amount = 1.0 - (diff / thickness);
            amount *= decay_factor;
            amount *= ev.intensity * intensity_scale_;
            contributions[k] += amount;
        }
    }

    for (std::size_t k = 0; k < total; ++k) {
        const double add = contributions[k];
        if (add <= 0.0) {
            continue;
        }
        auto color = frame.color(k);
        auto accumulate = [&](std::uint8_t base_channel, std::uint8_t ripple_channel) {
            const double value = static_cast<double>(base_channel) + static_cast<double>(ripple_channel) * add;
            return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 255));
        };
        color.r = accumulate(color.r, ripple_color_.r);
        color.g = accumulate(color.g, ripple_color_.g);
        color.b = accumulate(color.b, ripple_color_.b);
        frame.setColor(k, color);
    }
}

void ReactiveRipplePreset::buildCoords(const KeyboardModel& model) {
    const auto& layout = model.layout();
    const double rows = static_cast<double>(layout.size());
    double max_cols = 1.0;
    for (const auto& row : layout) {
        max_cols = std::max<double>(max_cols, row.size());
    }
    xs_.assign(model.keyCount(), 0.0);
    ys_.assign(model.keyCount(), 0.0);

    std::size_t idx = 0;
    for (std::size_t r = 0; r < layout.size(); ++r) {
        const auto& row = layout[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            if (idx >= xs_.size()) {
                break;
            }
            xs_[idx] = max_cols > 1.0 ? static_cast<double>(c) / (max_cols - 1.0) : 0.0;
            ys_[idx] = rows > 1.0 ? static_cast<double>(r) / (rows - 1.0) : 0.0;
            ++idx;
        }
    }
    coords_built_ = true;
}

RgbColor ReactiveRipplePreset::parseHexColor(const std::string& value) {
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
