#include "keyboard_configurator/hidapi_transport.hpp"

#include <iostream>

#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {
bool hidSucceeded(int rc, const char* action) {
    if (rc != 0) {
        std::cerr << "[HidapiTransport] Failed to " << action
                  << ": " << hid_error(nullptr) << '\n';
        return false;
    }
    return true;
}
}  // namespace

HidapiTransport::HidapiTransport() = default;

HidapiTransport::~HidapiTransport() {
    if (handle_) {
        hid_close(handle_.get());
    }
    hid_exit();
}

std::string HidapiTransport::id() const {
    return "hidapi";
}

bool HidapiTransport::ensureInitialized() {
    static bool initialized = false;
    if (!initialized) {
        if (hid_init() != 0) {
            std::cerr << "[HidapiTransport] hid_init failed" << '\n';
            return false;
        }
        initialized = true;
    }
    return true;
}

void HidapiTransport::HidDeleter::operator()(hid_device* device) const noexcept {
    if (device != nullptr) {
        hid_close(device);
    }
}

bool HidapiTransport::connect(const KeyboardModel& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureInitialized()) {
        return false;
    }

    handle_.reset(hid_open(model.vendorId(), model.productId(), nullptr));
    if (!handle_) {
        std::cerr << "[HidapiTransport] Unable to open device (VID="
                  << std::hex << model.vendorId()
                  << ", PID=" << model.productId() << std::dec << ")" << '\n';
        return false;
    }
    #ifdef __linux__
    hid_set_nonblocking(handle_.get(), 1);
    #endif
    std::cout << "[HidapiTransport] Connected to keyboard: " << model.name() << '\n';
    return true;
}

bool HidapiTransport::sendFrame(const KeyboardModel& model,
                                const std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_) {
        std::cerr << "[HidapiTransport] sendFrame called before connect" << '\n';
        return false;
    }

    int res = hid_send_feature_report(handle_.get(), payload.data(), static_cast<int>(payload.size()));
    if (res < 0) {
        std::cerr << "[HidapiTransport] send_feature_report failed for "
                  << model.name() << ": " << hid_error(handle_.get()) << '\n';
        return false;
    }
    return true;
}

}  // namespace kb::cfg
