#include "keyboard_configurator/key_activity_watcher.hpp"

#include <filesystem>
#include <linux/input-event-codes.h>
#include <libevdev/libevdev.h>
#include <unistd.h>
#include <fcntl.h>

#include <chrono>
#include <iostream>

namespace kb::cfg {

KeyActivityWatcher::KeyActivityWatcher(const KeyboardModel& model,
                                       KeyActivityProviderPtr provider)
    : model_(model), provider_(std::move(provider)) {}

KeyActivityWatcher::~KeyActivityWatcher() { stop(); }

void KeyActivityWatcher::start() {
    if (!provider_) return;
    if (thread_.joinable()) return;
    stop_.store(false);
    openDevices();
    provider_->setKeyCount(model_.keyCount());
    thread_ = std::thread(&KeyActivityWatcher::runLoop, this);
}

void KeyActivityWatcher::stop() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    closeDevices();
}

void KeyActivityWatcher::openDevices() {
    devices_.clear();
    const std::filesystem::path by_path("/dev/input/by-path");
    std::error_code ec;
    if (!std::filesystem::exists(by_path, ec)) {
        return;
    }
    for (auto& entry : std::filesystem::directory_iterator(by_path, ec)) {
        if (ec) break;
        if (!entry.is_symlink(ec) && !entry.is_regular_file(ec)) continue;
        const auto name = entry.path().filename().string();
        if (name.find("-kbd") == std::string::npos) continue;
        std::filesystem::path real = std::filesystem::read_symlink(entry.path(), ec);
        std::filesystem::path node = real.empty() ? entry.path() : (entry.path().parent_path() / real);
        int fd = ::open(node.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        libevdev* dev = nullptr;
        if (libevdev_new_from_fd(fd, &dev) != 0) {
            ::close(fd);
            continue;
        }
        devices_.push_back({fd, dev});
    }
}

void KeyActivityWatcher::closeDevices() {
    for (auto& d : devices_) {
        if (d.dev) {
            libevdev_free(d.dev);
            d.dev = nullptr;
        }
        if (d.fd >= 0) {
            ::close(d.fd);
            d.fd = -1;
        }
    }
    devices_.clear();
}

void KeyActivityWatcher::runLoop() {
    while (!stop_.load()) {
        for (auto& d : devices_) {
            if (!d.dev) continue;
            while (!stop_.load()) {
                input_event ev{};
                int rc = libevdev_next_event(d.dev, LIBEVDEV_READ_FLAG_NORMAL, &ev);
                if (rc == 0) {
                    if (ev.type == EV_KEY && ev.value == 1) { // key press
                        auto idx = model_.indexForKeycode(ev.code);
                        if (idx) {
                            provider_->recordKeyPress(*idx, 1.0);
                        }
                    }
                } else if (rc == -EAGAIN || rc == LIBEVDEV_READ_STATUS_SYNC || rc == -EINTR) {
                    break;
                } else {
                    break;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}  // namespace kb::cfg
