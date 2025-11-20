#include "keyboard_configurator/liquid_plasma_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace kb::cfg {

namespace {
RgbColor hsvToRgb(double h, double s, double v) {
    double c = v * s;
    double x = c * (1 - std::fabs(std::fmod(h / 60.0, 2) - 1));
    double m = v - c;
    double r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else { r = c; b = x; }
    return {
        static_cast<std::uint8_t>(std::clamp((r + m) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(std::clamp((g + m) * 255.0, 0.0, 255.0)),
        static_cast<std::uint8_t>(std::clamp((b + m) * 255.0, 0.0, 255.0))
    };
}

std::uint8_t mix8(std::uint8_t a, std::uint8_t b, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::clamp<int>(static_cast<int>(std::lround(a + (b - a) * t)), 0, 255));
}

RgbColor parseHexColor(const std::string& value) {
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
}

std::string LiquidPlasmaPreset::id() const { return "liquid_plasma"; }

void LiquidPlasmaPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("speed"); it != params.end()) speed_ = std::stod(it->second);
    if (auto it = params.find("scale"); it != params.end()) scale_ = std::stod(it->second);
    if (auto it = params.find("saturation"); it != params.end()) saturation_ = std::stod(it->second);
    if (auto it = params.find("value"); it != params.end()) value_ = std::stod(it->second);
    if (auto it = params.find("tint"); it != params.end()) { tint_ = parseHexColor(it->second); use_tint_ = true; }
    if (auto it = params.find("tint_mix"); it != params.end()) { use_tint_ = true; tint_mix_ = std::clamp(std::stod(it->second), 0.0, 1.0); }
}

void LiquidPlasmaPreset::buildCoords(const KeyboardModel& model) {
    const auto& layout = model.layout();
    std::size_t total = model.keyCount();
    xs_.assign(total, 0.0);
    ys_.assign(total, 0.0);
    std::size_t idx = 0;
    double rows = static_cast<double>(layout.size());
    double max_cols = 1.0;
    for (const auto& row : layout) max_cols = std::max<double>(max_cols, row.size());
    for (std::size_t r = 0; r < layout.size(); ++r) {
        const auto& row = layout[r];
        for (std::size_t c = 0; c < row.size(); ++c) {
            xs_[idx] = max_cols > 1.0 ? static_cast<double>(c) / (max_cols - 1.0) : 0.0;
            ys_[idx] = rows > 1.0 ? static_cast<double>(r) / (rows - 1.0) : 0.0;
            ++idx;
        }
    }
    coords_built_ = true;
}

void LiquidPlasmaPreset::render(const KeyboardModel& model,
                                double time_seconds,
                                KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) frame.resize(total);
    if (!coords_built_) buildCoords(model);

    double t = time_seconds * speed_ * 2.0 * 3.14159265358979323846;
    for (std::size_t i = 0; i < total; ++i) {
        double x = xs_[i] * scale_;
        double y = ys_[i] * scale_;
        double v = 0.0;
        v += std::sin(3.0 * x + t);
        v += std::sin(4.0 * (y + 0.25) + t * 1.37);
        v += std::sin(5.0 * (x + y) + t * 0.73);
        double r2 = x * x + y * y;
        v += std::sin(6.0 * std::sqrt(r2 + 1e-6) + t * 1.61);
        v = (v + 4.0) * 0.125;
        v = std::clamp(v, 0.0, 1.0);
        double hue = 360.0 * v;
        RgbColor c = hsvToRgb(hue, std::clamp(saturation_, 0.0, 1.0), std::clamp(value_, 0.0, 1.0));
        if (use_tint_) {
            c.r = mix8(c.r, tint_.r, tint_mix_);
            c.g = mix8(c.g, tint_.g, tint_mix_);
            c.b = mix8(c.b, tint_.b, tint_mix_);
        }
        frame.setColor(i, c);
    }
}

}  // namespace kb::cfg
