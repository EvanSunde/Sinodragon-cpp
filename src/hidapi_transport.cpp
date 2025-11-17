#include "keyboard_configurator/hidapi_transport.hpp"

#include <iostream>
#include <string>

#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

namespace {

std::string narrowError(hid_device* device) {
    const wchar_t* werror = hid_error(device);
    if (werror == nullptr) {
        return "unknown";
    }

    std::string result;
    while (*werror != L'\0') {
        wchar_t wc = *werror++;
        if (wc < 0x80) {
            result.push_back(static_cast<char>(wc));
        } else {
            result.push_back('?');
        }
    }
    return result;
}

bool matchesUsage(const hid_device_info* info,
                  const KeyboardModel& model) {
    if (info == nullptr) {
        return false;
    }

    const auto desired_page = model.interfaceUsagePage();
    const auto desired_usage = model.interfaceUsage();

    if (desired_page.has_value() && info->usage_page != desired_page.value()) {
        return false;
    }

    if (desired_usage.has_value() && info->usage != desired_usage.value()) {
        return false;
    }

    if (!desired_page.has_value() && !desired_usage.has_value()) {
        return info->usage_page == 0xFF00 && info->usage == 0x0001;
    }

    return true;
}

}  // namespace

HidapiTransport::HidapiTransport() = default;

HidapiTransport::~HidapiTransport() {
    handle_.reset();
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

hid_device* HidapiTransport::openMatchingInterface(const KeyboardModel& model) {
    hid_device_info* list = hid_enumerate(model.vendorId(), model.productId());
    if (!list) {
        return nullptr;
    }

    const hid_device_info* selected = nullptr;
    for (auto* current = list; current != nullptr; current = current->next) {
        if (matchesUsage(current, model)) {
            selected = current;
            break;
        }
    }

    hid_device* device = nullptr;
    if (selected != nullptr) {
        device = hid_open_path(selected->path);
    }

    if (!device && list != nullptr) {
        device = hid_open_path(list->path);
    }

    hid_free_enumeration(list);
    return device;
}

bool HidapiTransport::connect(const KeyboardModel& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureInitialized()) {
        return false;
    }

    handle_.reset(openMatchingInterface(model));
    if (!handle_) {
        handle_.reset(hid_open(model.vendorId(), model.productId(), nullptr));
    }
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
                  << model.name() << " (" << payload.size() << " bytes): "
                  << narrowError(handle_.get()) << '\n';
        return false;
    }
    return true;
}

}  // namespace kb::cfg
