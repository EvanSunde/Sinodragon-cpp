#include "keyboard_configurator/star_matrix_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace kb::cfg {

namespace {
inline RgbColor parseHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        return {255, 255, 255};
    }
    auto hexToComponent = [](char hi, char lo) -> std::uint8_t {
        auto hexValue = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (upper >= 'A' && upper <= 'F') return 10 + (upper - 'A');
            return 0;
        };
        return static_cast<std::uint8_t>((hexValue(hi) << 4) | hexValue(lo));
    };
    return {
        hexToComponent(value[1], value[2]),
        hexToComponent(value[3], value[4]),
        hexToComponent(value[5], value[6])
    };
}

inline std::uint8_t mix8(std::uint8_t a, std::uint8_t b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(a + (b - a) * t)), 0, 255));
}
} // namespace

StarMatrixPreset::StarMatrixPreset() = default;

std::string StarMatrixPreset::id() const { return "star_matrix"; }

void StarMatrixPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("star"); it != params.end()) {
        star_color_ = parseHexColor(it->second);
    }
    if (auto it = params.find("background"); it != params.end()) {
        background_ = parseHexColor(it->second);
    }
    if (auto it = params.find("density"); it != params.end()) {
        density_ = std::clamp(std::stod(it->second), 0.0, 1.0);
    }
    if (auto it = params.find("speed"); it != params.end()) {
        speed_ = std::max(0.0, std::stod(it->second));
    }
}

std::uint32_t StarMatrixPreset::hash32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

void StarMatrixPreset::render(const KeyboardModel& model,
                              double time_seconds,
                              KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) {
        frame.resize(total);
    }

    const double two_pi = 6.283185307179586;

    for (std::size_t idx = 0; idx < total; ++idx) {
        // Deterministic pseudo-random seed per key
        std::uint32_t h = hash32(static_cast<std::uint32_t>(idx + 1));
        double s = static_cast<double>(h % 10000) / 10000.0; // [0,1)
        double t = time_seconds * speed_ + s;
        double w = 0.5 * (1.0 + std::sin(two_pi * t)); // [0,1]
        // Keep lit only top density_ portion of cycle
        double threshold = 1.0 - density_;
        double b = 0.0;
        if (w > threshold) {
            b = (w - threshold) / std::max(1e-6, density_);
            b = std::clamp(b, 0.0, 1.0);
            // ease in/out
            b = b * b * (3.0 - 2.0 * b);
        }
        RgbColor c{};
        c.r = mix8(background_.r, star_color_.r, b);
        c.g = mix8(background_.g, star_color_.g, b);
        c.b = mix8(background_.b, star_color_.b, b);
        frame.setColor(idx, c);
    }
}

} // namespace kb::cfg
