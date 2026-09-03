#include "keyboard_configurator/logging_transport.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

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

    // Format into a local stream and emit in one write. Setting hex/fill
    // directly on std::cout mutates state shared with whatever thread is
    // printing the prompt, which is both a data race and visibly corrupts
    // the CLI output.
    std::ostringstream out;
    out << "[LoggingTransport] Sending frame for " << model.name() << " (" << payload.size()
        << " bytes):\n"
        << std::hex << std::setfill('0');

    std::size_t column = 0;
    for (auto byte : payload) {
        out << "0x" << std::setw(2) << static_cast<int>(byte) << ' ';
        if (++column == 16) {
            out << '\n';
            column = 0;
        }
    }
    if (column != 0) {
        out << '\n';
    }

    std::cout << out.str() << std::flush;
    return true;
}

}  // namespace kb::cfg
