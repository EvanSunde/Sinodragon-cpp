#include "keyboard_configurator/configurator_cli.hpp"

#include <chrono>
#include <iostream>
#include <thread>

#include "keyboard_configurator/effect_engine.hpp"

namespace kb::cfg {

ConfiguratorCLI::ConfiguratorCLI(const KeyboardModel& model, EffectEngine& engine)
    : model_(model), engine_(engine) {}

void ConfiguratorCLI::run() {
    std::cout << "Keyboard: " << model_.name() << '\n';
    std::cout << "Presets:" << '\n';
    const auto& presets = engine_.presetIds();
    for (const auto& id : presets) {
        std::cout << "  - " << id << '\n';
    }

    std::cout << "Streaming frames. Press Ctrl+C to exit." << '\n';

    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        engine_.renderFrame(elapsed);
        engine_.pushFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
}

}  // namespace kb::cfg
