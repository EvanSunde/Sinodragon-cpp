#include <exception>
#include <iostream>

#include "keyboard_configurator/config_loader.hpp"
#include "keyboard_configurator/configurator_cli.hpp"
#include "keyboard_configurator/effect_engine.hpp"

#include "keyboard_configurator/rainbow_wave_preset.hpp"
#include "keyboard_configurator/static_color_preset.hpp"
#include "keyboard_configurator/star_matrix_preset.hpp"
#include "keyboard_configurator/key_map_preset.hpp"

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

namespace {

PresetRegistry buildRegistry() {
    PresetRegistry registry;
    registry.registerPreset("static_color", [] { return std::make_unique<StaticColorPreset>(); });
    registry.registerPreset("rainbow_wave", [] { return std::make_unique<RainbowWavePreset>(); });
    registry.registerPreset("star_matrix", [] { return std::make_unique<StarMatrixPreset>(); });
    registry.registerPreset("key_map", [] { return std::make_unique<KeyMapPreset>(); });
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

        EffectEngine engine(runtime.model, *transport);
        engine.setPresets(std::move(runtime.presets), std::move(runtime.preset_masks));
        // Apply enabled flags from config
        for (std::size_t i = 0; i < runtime.preset_enabled.size(); ++i) {
            engine.setPresetEnabled(i, runtime.preset_enabled[i]);
        }

        ConfiguratorCLI cli(runtime.model,
                            engine,
                            std::move(runtime.preset_parameters),
                            runtime.frame_interval);
        cli.run();

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << "\n";
        return 1;
    }
}
