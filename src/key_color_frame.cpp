#include "keyboard_configurator/key_color_frame.hpp"

#include <stdexcept>

namespace kb::cfg {

void KeyColorFrame::setColor(std::size_t index, RgbColor color) {
    if (index >= colors_.size()) {
        throw std::out_of_range("KeyColorFrame::setColor index out of range");
    }
    colors_[index] = color;
}

RgbColor KeyColorFrame::color(std::size_t index) const {
    if (index >= colors_.size()) {
        throw std::out_of_range("KeyColorFrame::color index out of range");
    }
    return colors_[index];
}

void KeyColorFrame::fill(RgbColor color) {
    for (auto& entry : colors_) {
        entry = color;
    }
}

}  // namespace kb::cfg
