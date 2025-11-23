#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct libevdev;

#include "keyboard_configurator/key_activity.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

class KeyActivityWatcher {
public:
    KeyActivityWatcher(const KeyboardModel& model,
                       KeyActivityProviderPtr provider);
    ~KeyActivityWatcher();

    void start();
    void stop();

private:
    const KeyboardModel& model_;
    KeyActivityProviderPtr provider_;

    struct DevHandle {
        int fd{-1};
        libevdev* dev{nullptr};
    };

    std::vector<DevHandle> devices_;
    std::atomic<bool> stop_{false};
    std::thread thread_;

    void runLoop();
    void openDevices();
    void closeDevices();
};

}  // namespace kb::cfg
