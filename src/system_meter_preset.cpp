#include "keyboard_configurator/system_meter_preset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

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

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    std::string token;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == ',') {
            const auto begin = token.find_first_not_of(" \t");
            const auto end = token.find_last_not_of(" \t");
            if (begin != std::string::npos) {
                out.push_back(token.substr(begin, end - begin + 1));
            }
            token.clear();
        } else {
            token.push_back(text[i]);
        }
    }
    return out;
}

bool parseBool(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

}  // namespace

void SystemMeterPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("metric"); it != params.end()) {
        metric_ = it->second;
        std::transform(metric_.begin(), metric_.end(), metric_.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        // "custom:build" reads whatever `sinoctl metric build 0.5` last set.
        const auto colon = metric_.find(':');
        if (colon != std::string::npos) {
            custom_name_ = metric_.substr(colon + 1);
            metric_ = metric_.substr(0, colon);
        }
    }
    if (auto it = params.find("bar_keys"); it != params.end()) {
        bar_keys_ = splitList(it->second);
        resolved_valid_ = false;
    }
    if (auto it = params.find("smoothing"); it != params.end()) {
        try {
            smoothing_ = std::clamp(std::stod(it->second), 0.0, 0.99);
        } catch (...) {
        }
    }
    if (auto it = params.find("invert"); it != params.end()) {
        invert_ = parseBool(it->second);
    }
    if (auto it = params.find("pulse_when_charging"); it != params.end()) {
        pulse_when_charging_ = parseBool(it->second);
    }

    auto color = [&](const char* key, RgbColor& target) {
        if (auto it = params.find(key); it != params.end()) {
            target = parseHex(it->second, target);
        }
    };
    color("color_low", color_low_);
    color("color_mid", color_mid_);
    color("color_high", color_high_);
    color("color_empty", color_empty_);
}

double SystemMeterPreset::sample() {
    if (!state_) {
        return 0.0;
    }
    if (metric_ == "cpu") {
        return state_->cpuUsage();
    }
    if (metric_ == "memory" || metric_ == "ram") {
        return state_->memoryUsage();
    }
    if (metric_ == "load") {
        return state_->loadAverage();
    }
    if (metric_ == "battery") {
        double level = 0.0;
        bool charging = false;
        if (!state_->battery(level, charging)) {
            return 0.0;
        }
        charging_ = charging;
        return level;
    }
    return state_->metric(custom_name_.empty() ? metric_ : custom_name_);
}

RgbColor SystemMeterPreset::gradient(double position) const {
    // Green through amber to red across the length of the bar.
    if (position <= 0.5) {
        return mixColors(color_low_, color_mid_, position * 2.0);
    }
    return mixColors(color_mid_, color_high_, (position - 0.5) * 2.0);
}

void SystemMeterPreset::render(const KeyboardModel& model, double time_seconds,
                               KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_empty_);

    if (!resolved_valid_) {
        resolved_.clear();
        for (const auto& label : bar_keys_) {
            if (auto index = model.indexForKey(label)) {
                resolved_.push_back(*index);
            }
        }
        if (resolved_.empty()) {
            // No bar_keys given: use the whole board in packet order, so the
            // layer's own zones/keys mask decides what shows.
            for (std::size_t i = 0; i < model.keyCount(); ++i) {
                resolved_.push_back(i);
            }
        }
        resolved_valid_ = true;
    }

    double value = std::clamp(sample(), 0.0, 1.0);
    if (invert_) {
        value = 1.0 - value;
    }

    // Exponential smoothing towards the new reading.
    smoothed_ = (smoothed_ < 0.0) ? value : smoothed_ * smoothing_ + value * (1.0 - smoothing_);

    double brightness = 1.0;
    if (metric_ == "battery" && charging_ && pulse_when_charging_) {
        brightness = 0.55 + 0.45 * (0.5 + 0.5 * std::sin(time_seconds * 2.5));
    }

    const double filled = smoothed_ * static_cast<double>(resolved_.size());

    for (std::size_t i = 0; i < resolved_.size(); ++i) {
        const double position =
            resolved_.size() > 1 ? static_cast<double>(i) / (resolved_.size() - 1) : 0.0;

        // The last lit cell fades in proportionally, so the bar moves smoothly
        // rather than jumping a whole key at a time.
        const double fill = std::clamp(filled - static_cast<double>(i), 0.0, 1.0);
        if (fill <= 0.0) {
            continue;
        }

        RgbColor lit = gradient(position);
        if (brightness < 1.0) {
            lit = mixColors(color_empty_, lit, brightness);
        }
        frame.setColor(resolved_[i], mixColors(color_empty_, lit, fill));
    }
}

}  // namespace kb::cfg
