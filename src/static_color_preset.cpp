#include "keyboard_configurator/static_color_preset.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace kb::cfg {

namespace {
RgbColor parseHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        throw std::runtime_error("Expected colour string in format #RRGGBB");
    }
    auto hexToComponent = [](char hi, char lo) -> std::uint8_t {
        auto hexValue = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (upper >= 'A' && upper <= 'F') {
                return 10 + (upper - 'A');
            }
            throw std::runtime_error("Invalid hex digit in colour");
        };
        return static_cast<std::uint8_t>((hexValue(hi) << 4) | hexValue(lo));
    };

    return {
        hexToComponent(value[1], value[2]),
        hexToComponent(value[3], value[4]),
        hexToComponent(value[5], value[6])
    };
}
}  // namespace

StaticColorPreset::StaticColorPreset() = default;

std::string StaticColorPreset::id() const {
    return "static_color";
}

void StaticColorPreset::configure(const ParameterMap& params) {
    if (auto it = params.find("color"); it != params.end()) {
        color_ = parseHexColor(it->second);
    }
}

void StaticColorPreset::render(const KeyboardModel& model,
                               double /*time_seconds*/,
                               KeyColorFrame& frame) {
    (void)model;
    frame.fill(color_);
}

}  // namespace kb::cfg
