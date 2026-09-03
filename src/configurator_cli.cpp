#include "keyboard_configurator/configurator_cli.hpp"

#include <poll.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "keyboard_configurator/keyboard_model.hpp"
#include "keyboard_configurator/runtime.hpp"
#include "keyboard_configurator/shutdown_signal.hpp"

namespace kb::cfg {

ConfiguratorCLI::ConfiguratorCLI(Runtime& runtime) : runtime_(runtime) {}

void ConfiguratorCLI::printBanner() const {
    const auto& model = runtime_.model();
    std::cout << "Keyboard: " << model.name() << " (" << std::hex << model.vendorId() << ':'
              << model.productId() << std::dec << ")\n"
              << "Type 'help' for commands.\n";
}

void ConfiguratorCLI::run() {
    printBanner();
    std::cout << runtime_.execute("list") << '\n';

    std::string line;
    bool prompt_needed = true;

    while (!runtime_.shouldQuit() && !runtime_.configChanged() && !shutdownRequested()) {
        if (prompt_needed) {
            std::cout << "> " << std::flush;
            prompt_needed = false;
        }

        // Poll rather than blocking outright in getline, so that a config
        // change, a control-socket 'quit' or a signal can end the session
        // without the user having to press Enter first.
        pollfd stdin_poll{STDIN_FILENO, POLLIN, 0};
        const int ready = ::poll(&stdin_poll, 1, 200);
        if (ready < 0) {
            break;
        }
        if (ready == 0) {
            continue;
        }

        if (!std::getline(std::cin, line)) {
            break;
        }
        prompt_needed = true;

        const std::string reply = runtime_.execute(line);
        if (!reply.empty()) {
            std::cout << reply << '\n';
        }
    }

    std::cout << "Exiting configurator\n";
}

}  // namespace kb::cfg
