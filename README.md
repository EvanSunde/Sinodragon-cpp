# CPP version of Sinodragon

## Redragon keyboard RGB configurator 
This is a total rewrite of the RGB keyboard configurator in C++ for better performance and maintainability. Uses better Datastructures and algorithm for instant O(1) light highlighting. Supports very huge number of presets, and can be used in any way you want. Example config is given in `/configs/example.cfg`.


## Painter's Algorithm Rendering

Unlike traditional "On/Off" lighting engines, this daemon uses a Layer-Based approach similar to Photoshop.

- Define "Presets" (Assets) once in your library.

- Stack them infinitely in your Profiles.

- Mix effects: Put a Smoke effect behind a Static Color zone, or overlay a Ripple effect on top of Matrix Rain.

## ⚡ High-Performance Effects

Includes a suite of mathematically optimized C++ effects that run at <1% CPU usage.

- Liquid Plasma: Smooth, oily fluid mixing using sine-wave interference (0% CPU).

- Reaction Diffusion: Biologically inspired "living" patterns (Gray-Scott model).

- Smoke/Fog: Procedural Perlin/Simplex noise with directional wind drift.

- Doom Fire: The classic algorithm, optimized for low-res LED grids.

- Matrix Rain: Digital green cascading code.

## 🧠 Smart "Context-Aware" Automation

- Hyprland Integration: Automatically switches lighting profiles based on the active window class (e.g., Code → Yellow/Blue theme, Kitty → Amber Terminal theme).

- Shortcut Overlay: Hold Ctrl / Shift / Super to instantly highlight available hotkeys on your keyboard. (Supports app-specific shortcut maps!).

Other Features:
- Modular configurator architecture
- Configurable via text files
- 5 MB of Ram usages
- Easy Zone-based control (Any number of and any kind of zone can be made)
- In Hyprland, app based lighting can be controlled
- Supports App specific shortcut lighting
- CLI only App For Very Low Resource usages (0.1% CPU and 5 MB RAM)

## Modular configurator architecture

- **Core domain**
  - `KeyboardModel`: captures vendor/product IDs, packet header/length, physical layout (rows of key identifiers) and provides helpers to flatten layout indices and padding rules.
  - `KeyColorFrame`: immutable RGB buffer aligned with the model layout; responsible for zero-filling missing keys (`NAN`).
- **Preset system**
  - `LightingPreset` (abstract): exposes `apply(const KeyboardModel&, KeyColorFrame&)` to fill colours.
  - Built-in presets live under `presets/` (e.g. `StaticColorPreset`, `WavePreset`). Adding a preset = drop-in new subclass and register it.
- **Effect runtime**
  - `EffectEngine`: ticks time-aware presets, blending multiple layers, and pushes updates to the device backend.
- **Device backends**
  - `DeviceTransport` (abstract): encapsulates HID/USB operations (`connect`, `sendFrame`).
  - Linux HID implementation uses `hidapi` (`libhidapi-hidraw`) and the packet layout above.
- **Configuration & CLI**
  - `ConfigLoader`: parses the config to describe active keyboard model, enabled presets, and preset parameters.
  - `ConfiguratorCLI`: simple terminal UI to list models, preview presets, and activate them.

Extending the tool only requires dropping a new keyboard description JSON and implementing a preset subclass or registering a new transport.

## Build & run (Linux)

1. Install dependencies:
   - C++17 compiler
   - CMake ≥ 3.16
   - `hidapi` development headers (e.g. `sudo apt install libhidapi-dev`)
   - libevdev (for shortcut highlighting)
   - tomlplusplus (sudo pacman -Syu tomlplusplus)

2. Configure & build:
   ```bash
   mkdir -p keyboard_configurator/build
   cd keyboard_configurator/build
   cmake ..
   cmake --build .
   ```
3. Prepare a configuration file (examples live in `keyboard_configurator/configs/`). Use `transport = logging` for dry runs or `transport = hidapi` to drive the hardware.
4. Run the CLI:
   ```bash
   ./kb_configurator ../configs/config.toml
   ```
5. Inside the CLI, use `help`, `list`, `toggle <index>`, `set <index> <key> <value>`, and `frame <ms>` to control presets; `quit` exits.

### Frame timing

- Static-only configurations automatically emit a single frame on startup.
- When at least one animated preset is enabled, the CLI spawns a render loop. Control its cadence via either:
  - Config entry `engine.frame_interval_ms = <milliseconds>`
  - Runtime command `frame <milliseconds>` while the CLI is running

### HID interface selection

- The Linux transport defaults to vendor usage page `0xFF00` / usage `0x0001`, which is common for LED interfaces.
- Override the target interface in configs via:
  ```ini
  keyboard.interface_usage_page = 0xFF00
  keyboard.interface_usage = 0x0001
  ```
- If the hardware exposes a different custom usage pair, set those values accordingly. The transport falls back to the first interface when no match is found.

### Adding presets

1. Create a new subclass of `LightingPreset` in `include/keyboard_configurator/` and implement it under `src/`.
2. Override `render(...)` with your effect logic; if it’s animated, also override `isAnimated()` to return `true`.
3. Register the preset in `buildRegistry()` within `src/main.cpp` using `PresetRegistry::registerPreset`.
4. Reference it from a config file with `preset = your_preset_id ...` and expose any parameters via `ParameterMap` keys.

### Performance and Algorithms 

- Lookups: unordered_map O(1) average for app->profile and profile->sets.
- Applying a profile:
  - Enabled flags: O(P)
  - Masks: O(P·K) with P=presets, K=keys; occurs only on app change.
- Redundant events: skipped when active window class doesn’t change.

- vector<bool> and vector<vector<bool>> for O(P·K) apply on change only.

### Total packet structure
```
       vendor_id = 0x258A
       product_id = 0x0049
       device = None
       packet_header = [0x08, 0x0A, 0x7A, 0x01]
       packet_length = 382

packet creation layout 

            ["Esc", "`", "Tab", "Caps", "Shift", "Ctrl"],
            ["F1", "1", "Q", "A", "Z", "Win"],
            ["F2", "2", "W", "S", "X", "Alt"],
            ["F3", "3", "E", "D", "C", "NAN"],
            ["F4", "4", "R", "F", "V", "NAN"],
            ["F5", "5", "T", "G", "B", "Space"],
            ["F6", "6", "Y", "H", "N", "NAN"],
            ["F7", "7", "U", "J", "M", "NAN"],
            ["F8", "8", "I", "K", ",", "Alt"],
            ["F9", "9", "O", "L", ".", "Fn"],
            ["F10", "0", "P", ";", "/", "Ctrl"],
            ["F11", "-", "[", "'", "NAN", "NAN"],
            ["F12", "=", "]", "NAN", "NAN", "NAN"],
            ["PrtSc", "Bksp", "\\", "Enter", "Shift", "←"],
            ["Pause", "NAN", "NAN", "NAN", "↑", "↓"],
            ["Del", "Home", "End", "PgUp", "PgDn", "→"]

```
In this layout, each key has 00 00 00 as value which indicates R G B value. For the excess length to reach the packet length, add padding of 00. The NAN represents a non-existing key, so simply send 00 00 00.

This packet should be sent with the `send_feature_report` function.

### Happy hacking!

## Contributors
- @Evan (Lead Developer)