#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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

// Shared parsing helpers. Deliberately in their own namespace: several presets
// already carry a private parseHexColor, and this must not collide with them.
namespace color {

// "#RRGGBB" -> colour, returning `fallback` for anything malformed.
[[nodiscard]] RgbColor hex(const std::string& value, RgbColor fallback = {0, 0, 0});

// Splits a comma-separated list, trimming each element and dropping empties.
[[nodiscard]] std::vector<std::string> splitList(const std::string& text);

// A comma-separated list of "#RRGGBB". Empty when nothing valid was found, so
// the caller can keep its default palette.
[[nodiscard]] std::vector<RgbColor> palette(const std::string& text);

}  // namespace color

inline bool operator==(const RgbColor& lhs, const RgbColor& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

}  // namespace kb::cfg
