#include "keyboard_configurator/key_map_preset.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/key_color_frame.hpp"

namespace kb::cfg {

namespace {
RgbColor parseHexColor(const std::string& value) {
    if (value.size() != 7 || value.front() != '#') {
        return {0, 0, 0};
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
}  // namespace

KeyMapPreset::KeyMapPreset() = default;

std::string KeyMapPreset::id() const { return "key_map"; }

void KeyMapPreset::configure(const ParameterMap& params) {
    // background color optional
    if (auto it = params.find("background"); it != params.end()) {
        background_ = parseHexColor(it->second);
    }
    // keys specified as key.<Label>=#RRGGBB
    label_colors_.clear();
    for (const auto& kv : params) {
        const std::string& k = kv.first;
        if (k.rfind("key.", 0) == 0 && k.size() > 4) {
            std::string label = k.substr(4);
            label_colors_[label] = parseHexColor(kv.second);
        }
    }
}

void KeyMapPreset::render(const KeyboardModel& model,
                          double /*time_seconds*/,
                          KeyColorFrame& frame) {
    const auto total = model.keyCount();
    if (frame.size() != total) {
        frame.resize(total);
    }
    frame.fill(background_);
    for (const auto& kv : label_colors_) {
        if (auto index = model.indexForKey(kv.first)) {
            frame.setColor(*index, kv.second);
        }
    }
}

}  // namespace kb::cfg
