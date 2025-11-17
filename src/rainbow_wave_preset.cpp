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
    if (auto it = params.find("saturation"); it != params.end()) {
        saturation_ = std::stod(it->second);
    }
    if (auto it = params.find("value"); it != params.end()) {
        value_ = std::stod(it->second);
    }
    if (auto it = params.find("tint"); it != params.end()) {
        // parse #RRGGBB
        const std::string& v = it->second;
        if (v.size() == 7 && v[0] == '#') {
            auto hex = [](char c)->int{ if(c>='0'&&c<='9') return c-'0'; c=static_cast<char>(std::toupper(static_cast<unsigned char>(c))); if(c>='A'&&c<='F') return 10+(c-'A'); return 0; };
            tint_.r = static_cast<std::uint8_t>((hex(v[1])<<4)|hex(v[2]));
            tint_.g = static_cast<std::uint8_t>((hex(v[3])<<4)|hex(v[4]));
            tint_.b = static_cast<std::uint8_t>((hex(v[5])<<4)|hex(v[6]));
            use_tint_ = true;
        }
    }
    if (auto it = params.find("tint_mix"); it != params.end()) {
        use_tint_ = true;
        try {
            tint_mix_ = std::stod(it->second);
        } catch (...) {
            tint_mix_ = 0.5;
        }
        if (tint_mix_ < 0.0) tint_mix_ = 0.0;
        if (tint_mix_ > 1.0) tint_mix_ = 1.0;
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
        RgbColor c = hsvToRgb(phase, std::clamp(saturation_, 0.0, 1.0), std::clamp(value_, 0.0, 1.0));
        if (use_tint_) {
            auto mix = [&](std::uint8_t a, std::uint8_t b) {
                int v = static_cast<int>(std::lround(a * (1.0 - tint_mix_) + b * tint_mix_));
                return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
            };
            c.r = mix(c.r, tint_.r);
            c.g = mix(c.g, tint_.g);
            c.b = mix(c.b, tint_.b);
        }
        frame.setColor(idx, c);
    }
}

}  // namespace kb::cfg
