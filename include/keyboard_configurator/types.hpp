#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace kb::cfg {

struct RgbColor {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
};

using ParameterMap = std::unordered_map<std::string, std::string>;

// How a layer combines with what the layers below it have already painted.
// Normal is the historical behaviour: the layer simply overwrites.
enum class BlendMode {
    Normal,
    Add,
    Multiply,
    Screen,
};

struct LayerStyle {
    double opacity{1.0};
    BlendMode blend{BlendMode::Normal};
};

[[nodiscard]] BlendMode parseBlendMode(const std::string& name, bool* ok = nullptr);
[[nodiscard]] const char* blendModeName(BlendMode mode);

// Combines one key's incoming colour with what is already there.
[[nodiscard]] RgbColor blendColors(RgbColor below, RgbColor above, BlendMode mode);

// Mixes towards `above` by `amount` (0 keeps `below`, 1 takes `above`).
[[nodiscard]] RgbColor mixColors(RgbColor below, RgbColor above, double amount);

inline bool operator==(const RgbColor& lhs, const RgbColor& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

}  // namespace kb::cfg
