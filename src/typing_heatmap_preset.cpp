#include "keyboard_configurator/typing_heatmap_preset.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "keyboard_configurator/game_preset.hpp"
#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

RgbColor parseHex(const std::string& value, RgbColor fallback) {
    if (value.size() != 7 || value.front() != '#') {
        return fallback;
    }
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return 0;
    };
    return {static_cast<std::uint8_t>((digit(value[1]) << 4) | digit(value[2])),
            static_cast<std::uint8_t>((digit(value[3]) << 4) | digit(value[4])),
            static_cast<std::uint8_t>((digit(value[5]) << 4) | digit(value[6]))};
}

}  // namespace

void TypingHeatmapPreset::configure(const ParameterMap& params) {
    auto number = [&](const char* key, double& target, double minimum) {
        if (auto it = params.find(key); it != params.end()) {
            try {
                target = std::max(minimum, std::stod(it->second));
            } catch (...) {
            }
        }
    };
    number("half_life", half_life_, 0.2);
    number("gain", gain_, 0.001);
    number("ceiling", ceiling_, 0.05);
    if (auto it = params.find("spread"); it != params.end()) {
        try {
            spread_ = std::clamp(std::stod(it->second), 0.0, 1.0);
        } catch (...) {
        }
    }
    if (auto it = params.find("color_cold"); it != params.end()) {
        color_cold_ = parseHex(it->second, color_cold_);
    }
    if (auto it = params.find("palette"); it != params.end()) {
        std::vector<RgbColor> parsed;
        std::string token;
        const std::string& text = it->second;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == ',') {
                const auto begin = token.find_first_not_of(" \t");
                const auto end = token.find_last_not_of(" \t");
                if (begin != std::string::npos) {
                    const std::string hex = token.substr(begin, end - begin + 1);
                    if (hex.size() == 7 && hex[0] == '#') {
                        parsed.push_back(parseHex(hex, {255, 255, 255}));
                    }
                }
                token.clear();
            } else {
                token.push_back(text[i]);
            }
        }
        if (!parsed.empty()) {
            palette_ = std::move(parsed);
        }
    }
}

void TypingHeatmapPreset::setKeyActivityProvider(KeyActivityProviderPtr provider) {
    provider_ = std::move(provider);
    last_poll_ = provider_ ? provider_->nowSeconds() : 0.0;
}

RgbColor TypingHeatmapPreset::colorForHeat(double heat) const {
    if (palette_.empty()) {
        // Cold blue through cyan and yellow to hot white.
        static const std::vector<RgbColor> kDefault = {
            {0, 0, 40}, {0, 90, 200}, {0, 220, 200}, {220, 220, 40}, {255, 90, 20}, {255, 255, 255}};
        const double position = std::clamp(heat, 0.0, 1.0) * (kDefault.size() - 1);
        const auto low = static_cast<std::size_t>(position);
        const auto high = std::min(low + 1, kDefault.size() - 1);
        return mixColors(kDefault[low], kDefault[high], position - low);
    }
    const double position = std::clamp(heat, 0.0, 1.0) * (palette_.size() - 1);
    const auto low = static_cast<std::size_t>(position);
    const auto high = std::min(low + 1, palette_.size() - 1);
    return mixColors(palette_[low], palette_[high], position - low);
}

void TypingHeatmapPreset::render(const KeyboardModel& model, double time_seconds,
                                 KeyColorFrame& frame) {
    const auto key_count = model.keyCount();
    if (frame.size() != key_count) {
        frame.resize(key_count);
    }
    if (heat_.size() != key_count) {
        heat_.assign(key_count, 0.0);
        scratch_.assign(key_count, 0.0);
    }

    const double dt = (last_time_ < 0.0) ? 0.0 : std::max(0.0, time_seconds - last_time_);
    last_time_ = time_seconds;

    // Exponential decay, framerate-independent.
    if (dt > 0.0) {
        const double decay = std::exp(-dt / half_life_);
        for (auto& value : heat_) {
            value *= decay;
        }
    }

    if (provider_) {
        const double now = provider_->nowSeconds();
        const double window = std::max(0.0, now - last_poll_);
        last_poll_ = now;
        for (const auto& event : provider_->recentEvents(window)) {
            if (event.key_index < heat_.size()) {
                heat_[event.key_index] = std::min(ceiling_, heat_[event.key_index] + gain_);
            }
        }
    }

    // Bleed a little heat into physical neighbours so hot regions read as
    // regions rather than as isolated dots.
    if (spread_ > 0.0) {
        GameBoard board;
        board.build(model);
        scratch_ = heat_;
        for (int y = 0; y < board.height(); ++y) {
            for (int x = 0; x < board.width(); ++x) {
                const auto centre = board.index(x, y);
                if (!centre) {
                    continue;
                }
                double neighbour_sum = 0.0;
                int neighbour_count = 0;
                for (const auto& [dx, dy] : {std::pair{-1, 0}, std::pair{1, 0}, std::pair{0, -1},
                                             std::pair{0, 1}}) {
                    if (auto other = board.index(x + dx, y + dy)) {
                        neighbour_sum += heat_[*other];
                        ++neighbour_count;
                    }
                }
                if (neighbour_count > 0) {
                    const double average = neighbour_sum / neighbour_count;
                    scratch_[*centre] = heat_[*centre] * (1.0 - spread_) + average * spread_;
                }
            }
        }
        heat_.swap(scratch_);
    }

    for (std::size_t i = 0; i < key_count; ++i) {
        const double normalised = ceiling_ > 0.0 ? heat_[i] / ceiling_ : 0.0;
        frame.setColor(i, normalised <= 0.001 ? color_cold_ : colorForHeat(normalised));
    }
}

}  // namespace kb::cfg
