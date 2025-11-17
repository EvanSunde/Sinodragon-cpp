#pragma once

#include <vector>

#include <cstddef>

#include "keyboard_configurator/types.hpp"

namespace kb::cfg {

class KeyboardModel;

class KeyColorFrame {
public:
    explicit KeyColorFrame(std::size_t key_count = 0)
        : colors_(key_count) {}

    void resize(std::size_t key_count) { colors_.assign(key_count, RgbColor{}); }

    [[nodiscard]] std::size_t size() const noexcept { return colors_.size(); }

    void setColor(std::size_t index, RgbColor color);
    [[nodiscard]] RgbColor color(std::size_t index) const;

    void fill(RgbColor color);

    [[nodiscard]] std::vector<RgbColor>& colors() noexcept { return colors_; }
    [[nodiscard]] const std::vector<RgbColor>& colors() const noexcept { return colors_; }

private:
    std::vector<RgbColor> colors_;
};

}  // namespace kb::cfg
