#include "keyboard_configurator/rainbow_wave_preset.hpp"

#include <algorithm>
#include <cmath>

namespace kb::cfg {

namespace {
RgbColor hsvToRgb(double h, double s, double v) {
    double c = v * s;
    double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2) - 1));
    double m = v - c;

    double r = 0;
    double g = 0;
    double b = 0;

    if (h < 60) {
        r = c;
        g = x;
    } else if (h < 120) {
        r = x;
        g = c;
    } else if (h < 180) {
        g = c;
        b = x;
    } else if (h < 240) {
        g = x;
        b = c;
    } else if (h < 300) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }

    return {
        static_cast<std::uint8_t>(std::clamp((r + m) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(std::clamp((g + m) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(std::clamp((b + m) * 255.0, 0.0, 255.0))
    };
}
}  // namespace

std::string RainbowWavePreset::id() const {
    return "rainbow_wave";
}

void RainbowWavePreset::configure(const ParameterMap& params) {
    if (auto it = params.find("speed"); it != params.end()) {
        speed_ = std::stod(it->second);
    }
    if (auto it = params.find("scale"); it != params.end()) {
        scale_ = std::stod(it->second);
    }
}

void RainbowWavePreset::render(const KeyboardModel& model,
                               double time_seconds,
                               KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) {
        frame.resize(total);
    }

    for (std::size_t idx = 0; idx < total; ++idx) {
        double phase = (static_cast<double>(idx) * scale_ + time_seconds * speed_) * 360.0;
        phase = std::fmod(phase, 360.0);
        if (phase < 0) {
            phase += 360.0;
        }
        frame.setColor(idx, hsvToRgb(phase, 1.0, 1.0));
    }
}

}  // namespace kb::cfg
