#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <hidapi/hidapi.h>

#include "keyboard_configurator/device_transport.hpp"

namespace kb::cfg {

class HidapiTransport : public DeviceTransport {
public:
    HidapiTransport();
    ~HidapiTransport() override;

    std::string id() const override;
    bool connect(const KeyboardModel& model) override;
    bool sendFrame(const KeyboardModel& model,
                   const std::vector<std::uint8_t>& payload) override;

private:
    struct HidDeleter {
        void operator()(hid_device* device) const noexcept;
    };

    bool ensureInitialized();

    std::mutex mutex_;
    std::unique_ptr<hid_device, HidDeleter> handle_;
};

}  // namespace kb::cfg
