#include "keyboard_configurator/configurator_cli.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "keyboard_configurator/effect_engine.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

ConfiguratorCLI::ConfiguratorCLI(const KeyboardModel& model,
                                 EffectEngine& engine,
                                 std::vector<ParameterMap> preset_parameters)
    : model_(model),
      engine_(engine),
      preset_parameters_(std::move(preset_parameters)) {}

void ConfiguratorCLI::printBanner() const {
    std::cout << "Keyboard: " << model_.name() << " (" << model_.vendorId()
              << ":" << model_.productId() << ")" << '\n';
}

void ConfiguratorCLI::printHelp() const {
    std::cout << "Commands:" << '\n'
              << "  help                     - show this help" << '\n'
              << "  list                     - list presets" << '\n'
              << "  toggle <index>          - toggle preset on/off" << '\n'
              << "  set <index> <key> <val> - set preset parameter" << '\n'
              << "  frame <ms>              - set frame interval" << '\n'
              << "  quit                     - exit" << '\n';
}

void ConfiguratorCLI::printPresets() {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    const auto count = engine_.presetCount();
    std::cout << "Presets:" << '\n';
    for (std::size_t i = 0; i < count; ++i) {
        const auto& preset = engine_.presetAt(i);
        const bool enabled = engine_.presetEnabled(i);
        std::cout << "  [" << i << "] " << preset.id() << (enabled ? " (on)" : " (off)");
        if (i < preset_parameters_.size()) {
            std::cout << " params={";
            bool first = true;
            for (const auto& [key, value] : preset_parameters_[i]) {
                if (!first) {
                    std::cout << ", ";
                }
                std::cout << key << '=' << value;
                first = false;
            }
            std::cout << "}";
        }
        std::cout << '\n';
    }
}

bool ConfiguratorCLI::togglePreset(std::size_t index) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index >= engine_.presetCount()) {
        return false;
    }
    const bool current = engine_.presetEnabled(index);
    engine_.setPresetEnabled(index, !current);
    return true;
}

bool ConfiguratorCLI::setPresetParameter(std::size_t index,
                                         const std::string& key,
                                         const std::string& value) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index >= engine_.presetCount()) {
        return false;
    }

    if (index >= preset_parameters_.size()) {
        preset_parameters_.resize(engine_.presetCount());
    }

    preset_parameters_[index][key] = value;
    auto& preset = engine_.presetAt(index);
    preset.configure(preset_parameters_[index]);
    return true;
}

void ConfiguratorCLI::run() {
    printBanner();
    printHelp();
    printPresets();

    std::atomic<bool> stop{false};
    std::chrono::milliseconds frame_interval{33};

    std::thread render_thread([this, &stop, &frame_interval]() {
        auto start = std::chrono::steady_clock::now();
        while (!stop.load()) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();

            {
                std::lock_guard<std::mutex> guard(engine_mutex_);
                engine_.renderFrame(elapsed);
                engine_.pushFrame();
            }

            std::this_thread::sleep_for(frame_interval);
        }
    });

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) {
            continue;
        }

        if (cmd == "help") {
            printHelp();
        } else if (cmd == "list") {
            printPresets();
        } else if (cmd == "toggle") {
            std::size_t index = 0;
            if (!(iss >> index) || !togglePreset(index)) {
                std::cout << "Invalid preset index" << '\n';
            } else {
                std::cout << "Toggled preset " << index << '\n';
            }
        } else if (cmd == "set") {
            std::size_t index = 0;
            std::string key;
            std::string value;
            if (!(iss >> index >> key >> value) || !setPresetParameter(index, key, value)) {
                std::cout << "Invalid set command" << '\n';
            } else {
                std::cout << "Updated preset " << index << " parameter " << key << '\n';
            }
        } else if (cmd == "frame") {
            int interval_ms = 0;
            if (!(iss >> interval_ms) || interval_ms <= 0) {
                std::cout << "Invalid frame interval" << '\n';
            } else {
                frame_interval = std::chrono::milliseconds(interval_ms);
                std::cout << "Frame interval set to " << interval_ms << " ms" << '\n';
            }
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        } else {
            std::cout << "Unknown command" << '\n';
        }
    }

    stop.store(true);
    if (render_thread.joinable()) {
        render_thread.join();
    }

    std::cout << "Exiting configurator" << '\n';
}

}  // namespace kb::cfg
