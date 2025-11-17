#pragma once

#include <mutex>
#include <vector>

#include "keyboard_configurator/device_transport.hpp"

namespace kb::cfg {

class LoggingTransport : public DeviceTransport {
public:
    std::string id() const override;
    bool connect(const KeyboardModel& model) override;
    bool sendFrame(const KeyboardModel& model,
                   const std::vector<std::uint8_t>& payload) override;

private:
    std::mutex mutex_;
};

}  // namespace kb::cfg
