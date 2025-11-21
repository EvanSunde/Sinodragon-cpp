#include "keyboard_configurator/liquid_plasma_preset.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <string>

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
    if (auto it = params.find("wave_complexity"); it != params.end()) {
        try { wave_complexity_ = std::stoi(it->second); } catch (...) {}
        if (wave_complexity_ < 1) wave_complexity_ = 1;
        if (wave_complexity_ > 10) wave_complexity_ = 10;
    }
    if (auto it = params.find("mix_mode"); it != params.end()) {
        std::string m = it->second; std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (m == "nearest") mix_mode_ = MixMode::Nearest; else mix_mode_ = MixMode::Linear;
    }
    if (auto it = params.find("colors"); it != params.end()) {
        palette_.clear();
        const std::string& s = it->second;
        std::string token;
        for (std::size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == ',') {
                // trim spaces
                std::size_t a = 0, b = token.size();
                while (a < b && std::isspace(static_cast<unsigned char>(token[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>(token[b-1]))) --b;
                if (b > a) {
                    std::string hex = token.substr(a, b - a);
                    if (hex.size() == 7 && hex[0] == '#') {
                        palette_.push_back(parseHexColor(hex));
                        if (palette_.size() >= 10) break;
                    }
                }
                token.clear();
            } else {
                token.push_back(s[i]);
            }
        }
    }
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
        int terms = 0;
        // Directional sine components based on wave_complexity_
        for (int k = 0; k < wave_complexity_; ++k) {
            double ax = static_cast<double>(2 + k);
            double ay = static_cast<double>(3 + (k % 3));
            v += std::sin(ax * x + t * (1.0 + 0.31 * k)); ++terms;
            v += std::sin(ay * y + t * (0.73 + 0.17 * k)); ++terms;
            if ((k % 2) == 0) { v += std::sin((ax + ay) * (x + y) + t * (0.53 + 0.11 * k)); ++terms; }
        }
        // One radial term
        double r2 = x * x + y * y;
        v += std::sin((2.5 + 0.5 * wave_complexity_) * std::sqrt(r2 + 1e-6) + t * (1.0 + 0.21 * wave_complexity_));
        ++terms;
        // Normalize to [0,1]
        double v01 = (v + static_cast<double>(terms)) / (2.0 * static_cast<double>(terms));
        v01 = std::clamp(v01, 0.0, 1.0);

        RgbColor c{};
        if (!palette_.empty()) {
            if (palette_.size() == 1 || mix_mode_ == MixMode::Nearest) {
                std::size_t idx = static_cast<std::size_t>(std::lround(v01 * (palette_.size() - 1)));
                if (idx >= palette_.size()) idx = palette_.size() - 1;
                c = palette_[idx];
            } else {
                double pos = v01 * (palette_.size() - 1);
                std::size_t i0 = static_cast<std::size_t>(std::floor(pos));
                std::size_t i1 = std::min(i0 + 1, palette_.size() - 1);
                double f = pos - static_cast<double>(i0);
                const RgbColor& a = palette_[i0];
                const RgbColor& b = palette_[i1];
                c.r = mix8(a.r, b.r, f);
                c.g = mix8(a.g, b.g, f);
                c.b = mix8(a.b, b.b, f);
            }
        } else {
            double hue = 360.0 * v01;
            c = hsvToRgb(hue, std::clamp(saturation_, 0.0, 1.0), std::clamp(value_, 0.0, 1.0));
        }
        frame.setColor(i, c);
    }
}

}  // namespace kb::cfg
