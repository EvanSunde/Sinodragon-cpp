#include <exception>
#include <iostream>

#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/configurator_cli.hpp"
#include "keyboard_configurator/effect_engine.hpp"

#include "keyboard_configurator/key_activity.hpp"
#include "keyboard_configurator/key_activity_watcher.hpp"
#include "keyboard_configurator/doom_fire_preset.hpp"
#include "keyboard_configurator/rainbow_wave_preset.hpp"
#include "keyboard_configurator/static_color_preset.hpp"
#include "keyboard_configurator/star_matrix_preset.hpp"
#include "keyboard_configurator/key_map_preset.hpp"
#include "keyboard_configurator/liquid_plasma_preset.hpp"
#include "keyboard_configurator/reaction_diffusion_preset.hpp"
#include "keyboard_configurator/smoke_preset.hpp"
#include "keyboard_configurator/hyprland_watcher.hpp"
#include "keyboard_configurator/shortcut_watcher.hpp"

using kb::cfg::ConfigLoader;
using kb::cfg::ConfiguratorCLI;
using kb::cfg::DeviceTransport;
using kb::cfg::EffectEngine;
using kb::cfg::PresetRegistry;
using kb::cfg::RainbowWavePreset;
using kb::cfg::RuntimeConfig;
using kb::cfg::StaticColorPreset;
using kb::cfg::StarMatrixPreset;
using kb::cfg::KeyMapPreset;
using kb::cfg::DoomFirePreset;
using kb::cfg::LiquidPlasmaPreset;
using kb::cfg::ReactionDiffusionPreset;
using kb::cfg::SmokePreset;
using kb::cfg::HyprlandWatcher;
using kb::cfg::ShortcutWatcher;

namespace {

PresetRegistry buildRegistry() {
    PresetRegistry registry;
    registry.registerPreset("static_color", [] { return std::make_unique<StaticColorPreset>(); });
    registry.registerPreset("rainbow_wave", [] { return std::make_unique<RainbowWavePreset>(); });
    registry.registerPreset("star_matrix", [] { return std::make_unique<StarMatrixPreset>(); });
    registry.registerPreset("key_map", [] { return std::make_unique<KeyMapPreset>(); });
    registry.registerPreset("liquid_plasma", [] { return std::make_unique<LiquidPlasmaPreset>(); });
    registry.registerPreset("reaction_diffusion", [] { return std::make_unique<ReactionDiffusionPreset>(); });
    registry.registerPreset("smoke", [] { return std::make_unique<SmokePreset>(); });
    registry.registerPreset("doom_fire", [] { return std::make_unique<DoomFirePreset>(); });
    return registry;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        auto registry = buildRegistry();
        ConfigLoader loader(registry);

        std::string config_path = "configs/example.cfg";
        if (argc > 1) {
            config_path = argv[1];
        }

        RuntimeConfig runtime = loader.loadFromFile(config_path);

        auto transport = std::move(runtime.transport);
        if (!transport->connect(runtime.model)) {
            throw std::runtime_error("Failed to connect transport");
        }

        auto key_activity = std::make_shared<KeyActivityProvider>(runtime.model.keyCount());

        EffectEngine engine(runtime.model, *transport);
        engine.setKeyActivityProvider(key_activity);
        engine.setPresets(std::move(runtime.presets), std::move(runtime.preset_masks));
        // Apply enabled flags from config
        for (std::size_t i = 0; i < runtime.preset_enabled.size(); ++i) {
            engine.setPresetEnabled(i, runtime.preset_enabled[i]);
        }

        ConfiguratorCLI cli(runtime.model,
                            engine,
                            std::move(runtime.preset_parameters),
                            runtime.frame_interval);

        std::unique_ptr<KeyActivityWatcher> key_watcher;
        if (runtime.model.hasKeycodeMap()) {
            key_watcher = std::make_unique<KeyActivityWatcher>(runtime.model, key_activity);
            key_watcher->start();
        }

        std::unique_ptr<ShortcutWatcher> shortcuts;
        std::unique_ptr<HyprlandWatcher> hypr;
        if (runtime.hypr && runtime.hypr->enabled) {
            // Start shortcut watcher first so hypr callback can safely reference it
            if (runtime.hypr->shortcuts_overlay_preset_index >= 0) {
                shortcuts = std::make_unique<ShortcutWatcher>(runtime.model, cli, *runtime.hypr, runtime.model.keyCount());
                shortcuts->start();
            }
            hypr = std::make_unique<HyprlandWatcher>(*runtime.hypr, cli, engine.presetCount());
            if (shortcuts) {
                hypr->setActiveClassCallback([sw = shortcuts.get()](const std::string& klass){
                    sw->setActiveClass(klass);
                });
            }
            hypr->start();
        }

        cli.run();

        if (key_watcher) {
            key_watcher->stop();
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
