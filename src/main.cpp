#include <exception>
#include <iostream>
#include <memory>
#include <string>

#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/configurator_cli.hpp"
#include "keyboard_configurator/runtime.hpp"

#include "keyboard_configurator/doom_fire_preset.hpp"
#include "keyboard_configurator/hyprland_watcher.hpp"
#include "keyboard_configurator/key_activity_watcher.hpp"
#include "keyboard_configurator/key_map_preset.hpp"
#include "keyboard_configurator/liquid_plasma_preset.hpp"
#include "keyboard_configurator/rainbow_wave_preset.hpp"
#include "keyboard_configurator/reaction_diffusion_preset.hpp"
#include "keyboard_configurator/reactive_ripple_preset.hpp"
#include "keyboard_configurator/shortcut_watcher.hpp"
#include "keyboard_configurator/smoke_preset.hpp"
#include "keyboard_configurator/snake_preset.hpp"
#include "keyboard_configurator/space_colonization_preset.hpp"
#include "keyboard_configurator/star_matrix_preset.hpp"
#include "keyboard_configurator/static_color_preset.hpp"

using namespace kb::cfg;

namespace {

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
    return registry;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto registry = buildRegistry();
        ConfigLoader loader(registry);

        std::string config_path = "configs/config.toml";
        if (argc > 1) {
            config_path = argv[1];
        }

        // Reloading rebuilds the whole runtime; the loop is what lets the
        // config watcher restart the session in place of the process.
        while (true) {
            Runtime runtime(loader.loadFromFile(config_path), config_path);

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
            std::unique_ptr<HyprlandWatcher> hypr;
            if (runtime.hypr() && runtime.hypr()->enabled) {
                const HyprConfig& hypr_config = *runtime.hypr();
                if (hypr_config.shortcuts_overlay_preset_index >= 0) {
                    shortcuts = std::make_unique<ShortcutWatcher>(runtime.model(), runtime, hypr_config,
                                                                 runtime.model().keyCount());
                    shortcuts->start();
                }
                hypr = std::make_unique<HyprlandWatcher>(hypr_config, runtime, runtime.presetCount());
                if (shortcuts) {
                    hypr->setActiveClassCallback([watcher = shortcuts.get()](const std::string& klass) {
                        return watcher->setActiveClass(klass);
                    });
                }
                hypr->start();
            }

            ConfiguratorCLI cli(runtime);
            cli.run();

            if (key_watcher) {
                key_watcher->stop();
            }
            if (hypr) {
                hypr->stop();
            }
            if (shortcuts) {
                shortcuts->stop();
            }

            const bool reload = runtime.configChanged() && !runtime.shouldQuit();
            runtime.stop();

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
