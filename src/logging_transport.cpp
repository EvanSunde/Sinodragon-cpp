#include "keyboard_configurator/logging_transport.hpp"

#include <iomanip>
#include <iostream>

namespace kb::cfg {

std::string LoggingTransport::id() const {
    return "logging";
}

bool LoggingTransport::connect(const KeyboardModel& model) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[LoggingTransport] Connected to keyboard: " << model.name() << '\n';
    return true;
}

bool LoggingTransport::sendFrame(const KeyboardModel& model,
                                 const std::vector<std::uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "[LoggingTransport] Sending frame for " << model.name()
              << " (" << payload.size() << " bytes):" << '\n';
    std::cout << std::hex << std::setfill('0');
    std::size_t column = 0;
    for (auto byte : payload) {
        std::cout << "0x" << std::setw(2) << static_cast<int>(byte) << ' ';
        if (++column == 16) {
            std::cout << '\n';
            column = 0;
        }
    }
    if (column != 0) {
        std::cout << '\n';
    }
    std::cout << std::dec;
    return true;
}

}  // namespace kb::cfg
