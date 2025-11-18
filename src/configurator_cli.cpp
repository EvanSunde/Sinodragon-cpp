#include "keyboard_configurator/configurator_cli.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "keyboard_configurator/effect_engine.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

namespace kb::cfg {

ConfiguratorCLI::ConfiguratorCLI(const KeyboardModel& model,
                                 EffectEngine& engine,
                                 std::vector<ParameterMap> preset_parameters,
                                 std::chrono::milliseconds frame_interval)
    : model_(model),
      engine_(engine),
      preset_parameters_(std::move(preset_parameters)),
      stop_flag_(false),
      frame_interval_ms_(std::max(1, static_cast<int>(frame_interval.count()))),
      loop_running_(false) {}

ConfiguratorCLI::~ConfiguratorCLI() {
    stopRenderLoop();
}

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
              << "  frame <ms>              - set frame interval for animated presets" << '\n'
              << "  quit                     - exit" << '\n';
}

void ConfiguratorCLI::printPresets() {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    const auto count = engine_.presetCount();
    std::cout << "Presets:" << '\n';
    for (std::size_t i = 0; i < count; ++i) {
        const auto& preset = engine_.presetAt(i);
        const bool enabled = engine_.presetEnabled(i);
        std::cout << "  [" << i << "] " << preset.id()
                  << (enabled ? " (on" : " (off");
        if (preset.isAnimated()) {
            std::cout << ", animated";
        }
        std::cout << ")";

        if (i < preset_parameters_.size() && !preset_parameters_[i].empty()) {
            std::cout << " params={";
            bool first = true;
            for (const auto& [key, value] : preset_parameters_[i]) {
                if (!first) {
                    std::cout << ", ";
                }
                std::cout << key << '=' << value;
                first = false;
            }
            std::cout << '}';
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

bool ConfiguratorCLI::engineHasAnimated() const {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    return engine_.hasAnimatedEnabled();
}

void ConfiguratorCLI::renderOnce(double time_seconds) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    engine_.renderFrame(time_seconds);
    engine_.pushFrame();
}

void ConfiguratorCLI::startRenderLoop() {
    if (loop_running_.load()) {
        return;
    }

    stop_flag_.store(false);
    loop_running_.store(true);
    start_time_ = std::chrono::steady_clock::now();

    render_thread_ = std::thread([this]() {
        while (!stop_flag_.load()) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start_time_).count();
            renderOnce(elapsed);

            int interval = frame_interval_ms_.load();
            if (interval < 1) {
                interval = 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        }
        loop_running_.store(false);
    });
}

void ConfiguratorCLI::stopRenderLoop() {
    if (!loop_running_.load()) {
        return;
    }
    stop_flag_.store(true);
    if (render_thread_.joinable()) {
        render_thread_.join();
        render_thread_ = std::thread();
    }
    loop_running_.store(false);
}

void ConfiguratorCLI::syncRenderState(bool refresh_static_frame) {
    const bool animated = engineHasAnimated();
    if (animated) {
        if (!loop_running_.load()) {
            renderOnce(0.0);
            startRenderLoop();
        }
    } else {
        stopRenderLoop();
        if (refresh_static_frame) {
            renderOnce(0.0);
        }
    }
}

void ConfiguratorCLI::run() {
    printBanner();
    printHelp();
    printPresets();

    syncRenderState(true);

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
                syncRenderState(true);
                std::cout << "Toggled preset " << index << '\n';
            }
        } else if (cmd == "set") {
            std::size_t index = 0;
            std::string key;
            std::string value;
            if (!(iss >> index >> key >> value) || !setPresetParameter(index, key, value)) {
                std::cout << "Invalid set command" << '\n';
            } else {
                syncRenderState(true);
                std::cout << "Updated preset " << index << " parameter " << key << '\n';
            }
        } else if (cmd == "frame") {
            int interval_ms = 0;
            if (!(iss >> interval_ms) || interval_ms <= 0) {
                std::cout << "Invalid frame interval" << '\n';
            } else {
                frame_interval_ms_.store(interval_ms);
                std::cout << "Frame interval set to " << interval_ms << " ms" << '\n';
            }
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        } else {
            std::cout << "Unknown command" << '\n';
        }
    }

    stopRenderLoop();
    std::cout << "Exiting configurator" << '\n';
}

void ConfiguratorCLI::applyPresetEnable(std::size_t index, bool enabled) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index < engine_.presetCount()) {
        engine_.setPresetEnabled(index, enabled);
    }
}

void ConfiguratorCLI::applyPresetEnableSet(const std::vector<bool>& enabled) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    const auto count = engine_.presetCount();
    for (std::size_t i = 0; i < count && i < enabled.size(); ++i) {
        engine_.setPresetEnabled(i, enabled[i]);
    }
}

void ConfiguratorCLI::applyPresetMasks(const std::vector<std::vector<bool>>& masks) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    engine_.setPresetMasks(masks, true);
}

void ConfiguratorCLI::refreshRender() {
    syncRenderState(true);
}

void ConfiguratorCLI::applyPresetMask(std::size_t index, const std::vector<bool>& mask) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index < engine_.presetCount()) {
        engine_.setPresetMask(index, mask);
    }
}

void ConfiguratorCLI::applyPresetParameter(std::size_t index, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index >= engine_.presetCount()) return;
    if (index >= preset_parameters_.size()) {
        preset_parameters_.resize(engine_.presetCount());
    }
    preset_parameters_[index][key] = value;
    auto& preset = engine_.presetAt(index);
    preset.configure(preset_parameters_[index]);
}

}  // namespace kb::cfg


