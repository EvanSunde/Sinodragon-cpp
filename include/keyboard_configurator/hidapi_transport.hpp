#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <hidapi/hidapi.h>

#include "keyboard_configurator/device_transport.hpp"
#include "keyboard_configurator/retry_helper.hpp"

namespace kb::cfg {

class HidapiTransport : public DeviceTransport {
public:
    HidapiTransport();
    ~HidapiTransport() override;

    std::string id() const override;
    bool connect(const KeyboardModel& model) override;
    bool sendFrame(const KeyboardModel& model,
                   const std::vector<std::uint8_t>& payload) override;
    [[nodiscard]] bool isConnected() const override;

private:
    struct HidDeleter {
        void operator()(hid_device* device) const noexcept;
    };

    bool ensureInitialized();
    hid_device* openMatchingInterface(const KeyboardModel& model);

    // Callers hold mutex_ for all three.
    bool openLocked(const KeyboardModel& model);
    void closeLocked();
    bool reconnectLocked(const KeyboardModel& model);

    // A frame write can fail transiently -- a busy interface, a wakeup race --
    // so only treat the device as gone once several in a row have failed.
    static constexpr int kFailuresBeforeReset = 3;

    mutable std::mutex mutex_;
    std::unique_ptr<hid_device, HidDeleter> handle_;

    int consecutive_failures_{0};
    Backoff backoff_;
    bool reported_disconnect_{false};
    std::atomic<bool> connected_{false};
};

}  // namespace kb::cfg
