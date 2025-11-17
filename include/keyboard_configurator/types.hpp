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

inline bool operator==(const RgbColor& lhs, const RgbColor& rhs) {
    return lhs.r == rhs.r && lhs.g == rhs.g && lhs.b == rhs.b;
}

}  // namespace kb::cfg
