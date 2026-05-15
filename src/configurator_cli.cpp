#include "keyboard_configurator/configurator_cli.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

#include "keyboard_configurator/config_watcher.hpp"
#include "keyboard_configurator/effect_engine.hpp"
#include "keyboard_configurator/keyboard_model.hpp"

#include "keyboard_configurator/snake_preset.hpp"

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
      loop_running_(false),
      config_watch_enabled_(false),
      config_changed_(false) {}

ConfiguratorCLI::~ConfiguratorCLI() {
    stopConfigWatch();
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
              << "  snake <start|stop>      - start or stop snake game" << '\n'
              << "  watch <on|off>          - enable/disable config file watching" << '\n'
              << "  quit                     - exit" << '\n';
}

void ConfiguratorCLI::printPresets() {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    const auto count = engine_.presetCount();
    std::cout << "Presets:" << '\n';
    for (std::size_t i = 0; i < count; ++i) {
        const auto& preset = engine_.presetAt(i);
        // Note: We still use presetEnabled here for the 'toggle' command display
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

void ConfiguratorCLI::handleSnakeCommand(const std::string& arg) {
    bool should_refresh = false;
    bool should_override = false;
    bool should_clear_override = false;
    std::size_t snake_index = 0;

    {
        std::lock_guard<std::mutex> guard(engine_mutex_);
        for (std::size_t i = 0; i < engine_.presetCount(); ++i) {
            if (engine_.presetAt(i).id() == "snake") {
                auto* snake_preset = dynamic_cast<SnakePreset*>(&engine_.presetAt(i));
                if (snake_preset) {
                    if (arg == "start") {
                        snake_preset->start(model_);
                        engine_.setPresetEnabled(i, true);
                        snake_index = i;
                        should_override = true;
                        std::cout << "Snake game started!\n";
                    } else if (arg == "stop") {
                        snake_preset->stop();
                        engine_.setPresetEnabled(i, false);
                        should_clear_override = true;
                        std::cout << "Snake game stopped.\n";
                    } else {
                        std::cout << "Usage: snake <start|stop>\n";
                    }
                    should_refresh = true;
                    break;
                }
            }
        }
        if (!should_refresh) {
            std::cout << "Snake preset not found.\n";
        }
    }

    if (should_override) {
        applySnakeOverride(snake_index);
    } else if (should_clear_override) {
        clearSnakeOverride();
    }

    if (should_refresh) {
        syncRenderState(true);
    }
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
        // Check if config file has changed
        if (config_changed_.load()) {
            std::cout << "\nConfig file changed. Reloading...\n";
            break;
        }

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
        } else if (cmd == "snake") {
            std::string arg;
            if (iss >> arg) {
                handleSnakeCommand(arg);
            } else {
                std::cout << "Usage: snake <start|stop>\n";
            }
        } else if (cmd == "watch") {
            std::string arg;
            if (iss >> arg) {
                handleWatchCommand(arg);
            } else {
                std::cout << "Usage: watch <on|off>\n";
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

// --- WATCHER INTERFACE ---

void ConfiguratorCLI::setDrawList(const std::vector<std::size_t>& list) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (snake_override_active_) {
        saved_draw_list_ = list;
        saved_draw_list_valid_ = true;
        return;
    }
    current_draw_list_ = list;
    engine_.setDrawList(list);
}

void ConfiguratorCLI::applyPresetMasks(const std::vector<std::vector<bool>>& masks) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (snake_override_active_) {
        saved_masks_ = masks;
        saved_masks_valid_ = true;
        return;
    }
    current_masks_ = masks;
    engine_.setPresetMasks(masks, true);
}

void ConfiguratorCLI::refreshRender() {
    syncRenderState(true);
}

void ConfiguratorCLI::applyPresetMask(std::size_t index, const std::vector<bool>& mask) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index < engine_.presetCount()) {
        if (snake_override_active_) {
            if (saved_masks_.size() < engine_.presetCount()) {
                saved_masks_.resize(engine_.presetCount());
            }
            saved_masks_[index] = mask;
            saved_masks_valid_ = true;
            return;
        }
        if (current_masks_.size() < engine_.presetCount()) {
            current_masks_.resize(engine_.presetCount());
        }
        current_masks_[index] = mask;
        engine_.setPresetMask(index, mask);
    }
}

void ConfiguratorCLI::applyPresetParameter(std::size_t index, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> guard(engine_mutex_);
    if (index >= engine_.presetCount()) return;
    
    if (index >= preset_parameters_.size()) {
        preset_parameters_.resize(engine_.presetCount());
    }
    
    if (preset_parameters_[index][key] != value) {
        preset_parameters_[index][key] = value;
        auto& preset = engine_.presetAt(index);
        preset.configure(preset_parameters_[index]);
    }
}

void ConfiguratorCLI::applySnakeOverride(std::size_t snake_index)
{
    snake_override_active_ = true;

    saved_draw_list_ = current_draw_list_;
    saved_draw_list_valid_ = true;

    if (!current_masks_.empty()) {
        saved_masks_ = current_masks_;
        saved_masks_valid_ = true;
    } else {
        saved_masks_.clear();
        saved_masks_valid_ = false;
    }

    std::vector<std::size_t> snake_only = { snake_index };
    engine_.setDrawList(snake_only);
    current_draw_list_ = snake_only;

    std::vector<bool> full(model_.keyCount(), true);
    engine_.setPresetMask(snake_index, full);
}

void ConfiguratorCLI::clearSnakeOverride()
{
    if (!snake_override_active_)
        return;

    snake_override_active_ = false;

    if (saved_draw_list_valid_) {
        engine_.setDrawList(saved_draw_list_);
        current_draw_list_ = saved_draw_list_;
    } else {
        engine_.setDrawList(current_draw_list_);
    }

    if (saved_masks_valid_) {
        engine_.setPresetMasks(saved_masks_, true);
        current_masks_ = saved_masks_;
    }

    saved_draw_list_valid_ = false;
    saved_masks_valid_ = false;
    saved_draw_list_.clear();
    saved_masks_.clear();
}

// --- CONFIG WATCH INTERFACE ---

void ConfiguratorCLI::setConfigPath(const std::string& config_path) {
    std::lock_guard<std::mutex> guard(config_watch_mutex_);
    config_watcher_ = std::make_unique<ConfigWatcher>(config_path);
}

bool ConfiguratorCLI::isConfigChanged() const {
    return config_changed_.load();
}

void ConfiguratorCLI::handleWatchCommand(const std::string& arg) {
    if (arg == "on") {
        startConfigWatch();
    } else if (arg == "off") {
        stopConfigWatch();
    } else {
        std::cout << "Usage: watch <on|off>\n";
    }
}

void ConfiguratorCLI::startConfigWatch() {
    if (config_watch_enabled_.load()) {
        std::cout << "Config watch is already enabled.\n";
        return;
    }

    if (!config_watcher_) {
        std::cout << "Config watcher not initialized. Cannot enable watching.\n";
        return;
    }

    config_watch_enabled_.store(true);
    config_changed_.store(false);

    config_watch_thread_ = std::thread([this]() {
        while (config_watch_enabled_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (config_watcher_ && config_watcher_->hasChanged()) {
                config_changed_.store(true);
                break;
            }
        }
    });

    std::cout << "Config file watching enabled.\n";
}

void ConfiguratorCLI::stopConfigWatch() {
    if (!config_watch_enabled_.load()) {
        std::cout << "Config watch is already disabled.\n";
        return;
    }

    config_watch_enabled_.store(false);
    if (config_watch_thread_.joinable()) {
        config_watch_thread_.join();
        config_watch_thread_ = std::thread();
    }

    config_changed_.store(false);
    std::cout << "Config file watching disabled.\n";
}

}  // namespace kb::cfg
