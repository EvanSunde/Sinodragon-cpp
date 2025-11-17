#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "keyboard_configurator/key_color_frame.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

class DeviceTransport {
public:
    virtual ~DeviceTransport() = default;

    virtual std::string id() const = 0;
    virtual bool connect(const KeyboardModel& model) = 0;
    virtual bool sendFrame(const KeyboardModel& model,
                           const std::vector<std::uint8_t>& payload) = 0;
};

}  // namespace kb::cfg
