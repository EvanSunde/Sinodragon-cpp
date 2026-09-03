#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "keyboard_configurator/app_options.hpp"
#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/configurator_cli.hpp"
#include "keyboard_configurator/control_server.hpp"
#include "keyboard_configurator/runtime.hpp"
#include "keyboard_configurator/shutdown_signal.hpp"

#include "keyboard_configurator/doom_fire_preset.hpp"
#include "keyboard_configurator/key_activity_watcher.hpp"
#include "keyboard_configurator/key_map_preset.hpp"
#include "keyboard_configurator/liquid_plasma_preset.hpp"
#include "keyboard_configurator/rainbow_wave_preset.hpp"
#include "keyboard_configurator/reaction_diffusion_preset.hpp"
#include "keyboard_configurator/reactive_ripple_preset.hpp"
#include "keyboard_configurator/shortcut_watcher.hpp"
#include "keyboard_configurator/window_source.hpp"
#include "keyboard_configurator/smoke_preset.hpp"
#include "keyboard_configurator/life_preset.hpp"
#include "keyboard_configurator/pong_preset.hpp"
#include "keyboard_configurator/snake_preset.hpp"
#include "keyboard_configurator/tetris_preset.hpp"
#include "keyboard_configurator/typing_heatmap_preset.hpp"
#include "keyboard_configurator/space_colonization_preset.hpp"
#include "keyboard_configurator/star_matrix_preset.hpp"
#include "keyboard_configurator/static_color_preset.hpp"
#include "keyboard_configurator/status_light_preset.hpp"
#include "keyboard_configurator/system_meter_preset.hpp"

using namespace kb::cfg;

namespace {

constexpr const char* kVersion = "0.2.0";

PresetRegistry buildRegistry() {
    PresetRegistry registry;
    registry.registerPreset("static_color", [] { return std::make_unique<StaticColorPreset>(); });
    registry.registerPreset("rainbow_wave", [] { return std::make_unique<RainbowWavePreset>(); });
    registry.registerPreset("star_matrix", [] { return std::make_unique<StarMatrixPreset>(); });
    registry.registerPreset("key_map", [] { return std::make_unique<KeyMapPreset>(); });
    registry.registerPreset("liquid_plasma", [] { return std::make_unique<LiquidPlasmaPreset>(); });
    registry.registerPreset("reaction_diffusion", [] { return std::make_unique<ReactionDiffusionPreset>(); });
    registry.registerPreset("space_colonization", [] { return std::make_unique<SpaceColonizationPreset>(); });
    registry.registerPreset("smoke", [] { return std::make_unique<SmokePreset>(); });
    registry.registerPreset("doom_fire", [] { return std::make_unique<DoomFirePreset>(); });
    registry.registerPreset("reactive_ripple", [] { return std::make_unique<ReactiveRipplePreset>(); });
    registry.registerPreset("snake", [] { return std::make_unique<SnakePreset>(); });
    registry.registerPreset("tetris", [] { return std::make_unique<TetrisPreset>(); });
    registry.registerPreset("pong", [] { return std::make_unique<PongPreset>(); });
    registry.registerPreset("life", [] { return std::make_unique<LifePreset>(); });
    registry.registerPreset("typing_heatmap", [] { return std::make_unique<TypingHeatmapPreset>(); });
    registry.registerPreset("system_meter", [] { return std::make_unique<SystemMeterPreset>(); });
    registry.registerPreset("status_light", [] { return std::make_unique<StatusLightPreset>(); });
    return registry;
}

// Daemon mode has no prompt to sit in, so it just waits until something asks
// it to stop: a signal, a control-socket quit, or a config change.
void waitForShutdown(const Runtime& runtime) {
    while (!shutdownRequested() && !runtime.shouldQuit() && !runtime.configChanged()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

}  // namespace

int main(int argc, char** argv) {
    const AppOptions options = parseArgs(argc, argv);

    if (!options.valid) {
        std::cerr << "sinodragon: " << options.error << "\n\n" << usageText() << '\n';
        return 2;
    }
    if (options.show_help) {
        std::cout << usageText() << '\n';
        return 0;
    }
    if (options.show_version) {
        std::cout << "sinodragon " << kVersion << '\n';
        return 0;
    }

    installShutdownHandlers();

    try {
        const auto registry = buildRegistry();
        ConfigLoader loader(registry);
        if (options.preview) {
            loader.forceTransport("preview");
        }

        while (true) {
            Runtime runtime(loader.loadFromFile(options.config_path), options.config_path, loader);

            if (!runtime.connect()) {
                throw std::runtime_error("Failed to connect to device after retries");
            }

            runtime.start();

            std::unique_ptr<KeyActivityWatcher> key_watcher;
            if (runtime.model().hasKeycodeMap()) {
                key_watcher = std::make_unique<KeyActivityWatcher>(runtime.model(), runtime.keyActivity());
                key_watcher->start();
            }

            std::unique_ptr<ShortcutWatcher> shortcuts;
            std::unique_ptr<WindowSource> windows;

            // A snapshot, because the config watcher may already be running and
            // a reload replaces the runtime's copy underneath us.
            const std::optional<HyprConfig> hypr_snapshot = runtime.hyprConfig();
            if (hypr_snapshot && hypr_snapshot->enabled) {
                const HyprConfig& hypr_config = *hypr_snapshot;
                if (hypr_config.shortcuts_overlay_preset_index >= 0) {
                    shortcuts = std::make_unique<ShortcutWatcher>(runtime.model(), runtime, hypr_config,
                                                                 runtime.model().keyCount());
                    shortcuts->start();
                    // A reload rebuilds the shortcut tables in place rather
                    // than restarting the evdev watcher.
                    runtime.setConfigObserver([watcher = shortcuts.get()](const HyprConfig& updated) {
                        watcher->reconfigure(updated);
                    });
                }

                windows = createWindowSource(hypr_config.window_source, hypr_config.events_socket);
                if (windows) {
                    std::cout << "[Window] Using the " << windows->id() << " backend.\n";
                    windows->start([&runtime, watcher = shortcuts.get()](const WindowInfo& info) {
                        // The shortcut overlay gets first refusal: while a
                        // modifier is held it owns the display, and swapping
                        // the profile underneath it would flicker.
                        const bool overlay_engaged =
                            watcher != nullptr && watcher->setActiveWindow(info.window_class, info.title);
                        if (!overlay_engaged) {
                            runtime.activateProfileForWindow(info.window_class, info.title);
                        }
                    });
                }
            }

            std::unique_ptr<ControlServer> control;
            if (options.enable_socket) {
                control = std::make_unique<ControlServer>(
                    runtime, options.socket_path.empty() ? defaultControlSocketPath()
                                                         : options.socket_path);
                if (!control->start()) {
                    // A daemon that cannot be controlled is still better than
                    // no daemon; carry on with the lighting.
                    std::cerr << "[Main] Continuing without a control socket.\n";
                    control.reset();
                }
            }

            if (options.daemon) {
                std::cout << "[Main] Running in daemon mode; send SIGTERM to stop.\n" << std::flush;
                waitForShutdown(runtime);
            } else {
                ConfiguratorCLI cli(runtime);
                cli.run();
            }

            if (control) {
                control->stop();
            }
            if (key_watcher) {
                key_watcher->stop();
            }
            if (windows) {
                windows->stop();
            }
            if (shortcuts) {
                shortcuts->stop();
            }

            const bool reload = runtime.configChanged() && !runtime.shouldQuit() && !shutdownRequested();

            if (shutdownRequested()) {
                std::cout << "[Main] Signal " << shutdownSignal() << "; shutting down.\n";
            }

            // Stop the render thread *before* blanking: otherwise the next
            // frame it pushes lands on top of the black one and the keyboard
            // stays lit after we exit.
            runtime.stop();

            if (!reload) {
                runtime.blank();
            }

            if (!reload) {
                break;
            }
            std::cout << "[Main] Reloading configuration...\n";
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
