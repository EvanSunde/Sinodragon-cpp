#include "keyboard_configurator/status_light_preset.hpp"

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

}  // namespace

void StatusLightPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("signal"); it != params.end()) {
        signal_ = it->second;
    }
    if (auto it = params.find("keys"); it != params.end()) {
        keys_ = splitList(it->second);
        resolved_valid_ = false;
    }
    if (auto it = params.find("ok_timeout"); it != params.end()) {
        try {
            ok_timeout_ = std::max(0.0, std::stod(it->second));
        } catch (...) {
        }
    }
    if (auto it = params.find("color_off"); it != params.end()) {
        color_off_ = parseHex(it->second, color_off_);
    }

    // Sensible defaults, each overridable as color_<state> / style_<state>.
    styles_ = {
        {"ok", {{0, 220, 80}, Style::Solid}},
        {"warn", {{240, 180, 0}, Style::Pulse}},
        {"fail", {{255, 30, 20}, Style::Pulse}},
        {"busy", {{255, 150, 0}, Style::Sweep}},
    };

    for (const auto& [key, value] : params) {
        if (key.rfind("color_", 0) == 0 && key != "color_off") {
            const std::string name = key.substr(6);
            styles_[name].color = parseHex(value, styles_[name].color);
        } else if (key.rfind("style_", 0) == 0) {
            const std::string name = key.substr(6);
            std::string style = value;
            std::transform(style.begin(), style.end(), style.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (style == "pulse") {
                styles_[name].style = Style::Pulse;
            } else if (style == "sweep") {
                styles_[name].style = Style::Sweep;
            } else {
                styles_[name].style = Style::Solid;
            }
        }
    }
}

StatusLightPreset::Appearance StatusLightPreset::appearanceFor(const std::string& value) const {
    auto it = styles_.find(value);
    if (it != styles_.end()) {
        return it->second;
    }
    // An unrecognised state still shows something, so a typo in a script is
    // visible rather than silent.
    return Appearance{{120, 120, 120}, Style::Solid};
}

void StatusLightPreset::render(const KeyboardModel& model, double time_seconds,
                               KeyColorFrame& frame) {
    if (frame.size() != model.keyCount()) {
        frame.resize(model.keyCount());
    }
    frame.fill(color_off_);

    if (!state_) {
        return;
    }

    const std::string value = state_->state(signal_);
    if (value.empty() || value == "off" || value == "none") {
        return;
    }

    if (!resolved_valid_) {
        resolved_.clear();
        for (const auto& label : keys_) {
            if (auto index = model.indexForKey(label)) {
                resolved_.push_back(*index);
            }
        }
        if (resolved_.empty()) {
            for (std::size_t i = 0; i < model.keyCount(); ++i) {
                resolved_.push_back(i);
            }
        }
        resolved_valid_ = true;
    }

    const double age = state_->stateAge(signal_);
    const Appearance appearance = appearanceFor(value);

    // A successful build should not stay lit all afternoon.
    double fade = 1.0;
    if (value == "ok" && ok_timeout_ > 0.0) {
        if (age > ok_timeout_) {
            return;
        }
        fade = std::clamp(1.0 - (age / ok_timeout_), 0.0, 1.0);
    }

    for (std::size_t i = 0; i < resolved_.size(); ++i) {
        double intensity = 1.0;
        switch (appearance.style) {
            case Style::Solid:
                break;
            case Style::Pulse:
                intensity = 0.35 + 0.65 * (0.5 + 0.5 * std::sin(time_seconds * 5.0));
                break;
            case Style::Sweep: {
                // A band travelling along the run of keys.
                const double position =
                    resolved_.size() > 1 ? static_cast<double>(i) / (resolved_.size() - 1) : 0.0;
                const double head = std::fmod(time_seconds * 0.8, 1.0);
                double distance = std::fabs(position - head);
                distance = std::min(distance, 1.0 - distance);  // wrap around
                intensity = std::clamp(1.0 - distance * 5.0, 0.12, 1.0);
                break;
            }
        }
        frame.setColor(resolved_[i], mixColors(color_off_, appearance.color, intensity * fade));
    }
}

}  // namespace kb::cfg
