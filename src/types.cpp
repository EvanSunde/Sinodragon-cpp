#include "keyboard_configurator/types.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kb::cfg {

namespace {

inline std::uint8_t clamp8(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

}  // namespace

BlendMode parseBlendMode(const std::string& name, bool* ok) {
    std::string lowered;
    lowered.reserve(name.size());
    for (char ch : name) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    if (ok != nullptr) {
        *ok = true;
    }
    if (lowered == "normal" || lowered == "replace" || lowered.empty()) return BlendMode::Normal;
    if (lowered == "add" || lowered == "additive") return BlendMode::Add;
    if (lowered == "multiply") return BlendMode::Multiply;
    if (lowered == "screen") return BlendMode::Screen;

    if (ok != nullptr) {
        *ok = false;
    }
    return BlendMode::Normal;
}

const char* blendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Add: return "add";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Screen: return "screen";
        case BlendMode::Normal: break;
    }
    return "normal";
}

RgbColor blendColors(RgbColor below, RgbColor above, BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:
            return above;
        case BlendMode::Add:
            return {clamp8(below.r + above.r), clamp8(below.g + above.g), clamp8(below.b + above.b)};
        case BlendMode::Multiply:
            return {clamp8(below.r * above.r / 255), clamp8(below.g * above.g / 255),
                    clamp8(below.b * above.b / 255)};
        case BlendMode::Screen:
            return {clamp8(255 - (255 - below.r) * (255 - above.r) / 255),
                    clamp8(255 - (255 - below.g) * (255 - above.g) / 255),
                    clamp8(255 - (255 - below.b) * (255 - above.b) / 255)};
    }
    return above;
}

RgbColor mixColors(RgbColor below, RgbColor above, double amount) {
    const double t = std::clamp(amount, 0.0, 1.0);
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
        return clamp8(static_cast<int>(std::lround(a + (b - a) * t)));
    };
    return {mix(below.r, above.r), mix(below.g, above.g), mix(below.b, above.b)};
}

}  // namespace kb::cfg
