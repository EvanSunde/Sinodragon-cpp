# Sinodragon

A layer-compositing RGB lighting daemon for Redragon keyboards, written in C++.

Effects are stacked like layers in an image editor, grouped into profiles, and
switched automatically by whichever window has focus. It runs as a background
daemon and is driven at runtime through a control socket, so lighting can be
bound to a key, changed from a script, or wired into CI.

```bash
sinodragon --daemon        # run it
sinoctl profile magma      # switch profiles
sinoctl brightness 40      # dim it
sinoctl game tetris start  # play something
sinoctl state build fail   # turn the F row red from a CI script
```

---

## Contents

- [Install](#install)
- [Running it](#running-it)
- [Commands](#commands)
- [Configuration](#configuration)
- [Effects](#effects)
- [Games](#games)
- [System-state layers](#system-state-layers)
- [Architecture](#architecture)
- [Packet format](#packet-format)

---

## Install

Dependencies:

- A C++17 compiler and CMake ≥ 3.16
- `hidapi` (found via its CMake package or via pkg-config)
- `libevdev` — reactive effects, the shortcut overlay and the games
- `tomlplusplus`
- `libX11` — optional, only for the X11 window backend

```bash
# Arch
sudo pacman -S hidapi libevdev tomlplusplus cmake

# Debian / Ubuntu
sudo apt install libhidapi-dev libevdev-dev libtomlplusplus-dev cmake
```

Build and install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo cmake --install build
```

That installs `sinodragon`, `sinoctl`, the udev rules and a systemd user unit.

### Permissions

The daemon needs to open the keyboard's `hidraw` node, and — for reactive
effects, the shortcut overlay and the games — to read `/dev/input/event*`.
Both are root-only by default:

```bash
sudo cp packaging/70-sinodragon.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The rules use `TAG+="uaccess"`, which grants access to the locally logged-in
user. That is deliberately *not* "add yourself to the `input` group" — group
membership would let every process you run read your keystrokes.

If your keyboard is not a `258a` device, change the vendor id in the rules
file; `lsusb` will tell you what it is.

### Autostart

```bash
mkdir -p ~/.config/systemd/user
cp packaging/sinodragon.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now sinodragon
```

---

## Running it

```
sinodragon [options] [config.toml]

  -c, --config <path>   Config file to load
  -d, --daemon          Run without the interactive prompt
  -p, --preview         Draw frames in the terminal instead of sending them
                        to the keyboard (implies --daemon)
  -s, --socket <path>   Control socket to listen on
      --no-socket       Do not listen for control commands
      --lock <path>     Single-instance lock file (default: socket path + .lock)
      --no-lock         Allow more than one instance (not recommended)
  -h, --help            Show this help
  -v, --version         Show the version
```

With no config argument it looks for `$XDG_CONFIG_HOME/sinodragon/config.toml`,
then `~/.config/sinodragon/config.toml`, then `./configs/config.toml`.

Only one daemon runs at a time: a second one refuses to start (exit code 3)
rather than fighting the first over the keyboard. The lock is released
automatically when the daemon exits, crash included. To drive two keyboards
with two daemons, give each its own `--socket` (which gives each its own lock);
`--no-lock` disables the check entirely.

`--preview` renders each frame as coloured blocks laid out in the physical key
grid. It decodes the same bytes that would go to the device, so what you see
includes master brightness — useful for building effects with no keyboard
attached.

SIGINT and SIGTERM blank the keyboard and exit cleanly rather than leaving the
last frame frozen on it.

---

## Commands

The same commands work at the interactive prompt and through `sinoctl`.

| Command | Description |
| --- | --- |
| `status` | Device, transport, active profile, brightness, running game |
| `list` | Presets, and which are currently drawn |
| `profiles` | Configured profiles |
| `profile <name>` | Activate a profile |
| `brightness [0-100]` | Get or set master brightness |
| `frame <ms>` | Animation frame interval |
| `set <index> <key> <value>` | Change a preset parameter live |
| `toggle <index>` | Toggle one preset on or off |
| `game list` | Configured games |
| `game <name> <start\|stop>` | Run a game; `game stop` stops whichever is running |
| `metric <name> <0..1>` | Feed a value to a `system_meter` layer |
| `state <name> <value>` | Set a `status_light` state |
| `reload` | Re-read the config in place |
| `watch <on\|off>` | Watch the config file for changes |
| `quit` | Shut the daemon down |

`sinoctl` exits non-zero when the daemon rejects a command, so scripts can
branch on it:

```bash
sinoctl profile "$1" || notify-send "no such profile: $1"
```

The socket lives at `$XDG_RUNTIME_DIR/sinodragon.sock`, mode 0600.

### Binding to a key

```bash
# Hyprland
bind = SUPER, F1, exec, sinoctl profile coding
bind = SUPER, F2, exec, sinoctl brightness 20
```

---

## Configuration

One TOML file. See `configs/config.toml` for a full working example.

### `[device]`

| Key | Default | Meaning |
| --- | --- | --- |
| `name` | `"Unknown Device"` | Display name |
| `vendor_id`, `product_id` | `0` | USB ids |
| `packet_header` | `[]` | Bytes prepended to every report |
| `packet_length` | `0` | Total report size |
| `layout` | — | CSV of key labels, relative to the config file |
| `keycodes` | — | CSV of evdev names; without it reactive effects and games are inert |
| `transport` | `"hidapi"` | `hidapi`, `preview` or `logging` |
| `frame_interval_ms` | `33` | Animation tick |
| `brightness` | `100` | Master brightness, 0–100 |
| `config_watch_mode` | `false` | Watch the config file from startup |
| `preview_transpose` | auto | Override the preview's grid orientation |

### `[hypr]`

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `false` | Enables window watching and the shortcut overlay |
| `window_source` | `"auto"` | `auto`, `hyprland`, `sway`, `x11` or `none` |
| `events_socket` | auto | Override the compositor socket path |
| `shortcuts_overlay_effect` | — | Inline effect drawn while a modifier is held |

### `[zones]`

Named sets of key labels, referenced by a layer's `zones`.

```toml
[zones]
  function = ["F1", "F2", "F3", "F4", "F5", "F6"]
  wasd = ["W", "A", "S", "D"]
```

Labels must match the layout CSV exactly.

### `[profiles.<name>]`

A profile is an ordered stack of layers, drawn bottom to top.

```toml
[[profiles.coding.layers]]
  type = "static_color"
  color = "#FFFF40"

[[profiles.coding.layers]]
  type = "rainbow_wave"
  speed = 0.8
  zones = ["function"]      # restrict to a zone
  blend = "screen"          # normal | add | multiply | screen
  opacity = 0.6             # 0.0 - 1.0
```

- `type` names the effect; everything else is passed to it as a parameter.
- `zones` and `keys` restrict which keys the layer touches. With neither, it
  covers the whole board and paints over everything below it.
- `blend` and `opacity` control how it combines with the layers below. The
  default — opaque `normal` — simply overwrites, which is the historical
  behaviour.

### `[apps]`

```toml
[apps]
  default_profile = "liquid_n"
  default_shortcut = "default"

[apps.mappings]
  Code = "coding"
  kitty = "maze"
  [apps.mappings.zen]
    profile = "food"
    shortcut = "zen"

# Title rules are checked before class mappings; first match wins.
[[apps.title_rules]]
  contains = "youtube"       # case-insensitive substring
  class = "firefox"          # optional, restricts the rule
  profile = "video"
```

### `[shortcuts.<name>]`

```toml
[shortcuts.kitty]
  color = "#ff5500"
  ctrl = ["C", "D", "L", "Z"]
  ctrl_shift = ["C", "V", "T", "W"]
```

Modifier tokens joined with `_`, any order and case: `ctrl`, `shift`, `alt`,
`super`/`win`/`meta`. The overlay engages on an exact match, so `ctrl` and
`ctrl_shift` are separate entries.

### Hot reload

`reload`, or `config_watch_mode = true`, re-reads the config without dropping
the device handle or restarting the watchers. The active profile is kept if it
still exists. A config that fails to parse is rejected and the running one is
kept, so saving a half-written file will not black out your keyboard. Changing
the `[device]` section needs a new handle, so that one restarts the runtime.

---

## Effects

| Effect | Notes |
| --- | --- |
| `static_color` | `color` |
| `key_map` | Per-label colours (`key.<Label>`), `background` |
| `rainbow_wave` | `speed`, `scale`, `saturation`, `value`, `tint`, `tint_mix` |
| `star_matrix` | `star`, `background`, `density`, `speed` |
| `liquid_plasma` | Sine interference. `colors`, `scale`, `wave_complexity`, `mix_mode` |
| `smoke` | fBm noise with wind. `octaves`, `persistence`, `lacunarity`, `drift_x/y`, `contrast` |
| `reaction_diffusion` | Gray-Scott. `feed`, `kill`, `du`, `dv`, `steps`, `zoom` |
| `doom_fire` | `cooling`, `spark_chance`, `spark_intensity`, `palette` |
| `reactive_ripple` | Rings from keystrokes. `wave_speed`, `decay_time`, `thickness` |
| `space_colonization` | Growing roots. `attractors`, `kill_dist`, `segment_len`, `lifespan` |
| `typing_heatmap` | Where you type. `half_life`, `gain`, `spread`, `palette` |
| `system_meter` | A value as a bar — see below |
| `status_light` | An externally driven state — see below |

`liquid_plasma`, `smoke`, `reaction_diffusion` and `space_colonization` accept
`reactive = true` and a family of `reactive_*` parameters that warp the field
around recent keystrokes. `configs/config_preset_info.md` has the per-parameter
detail.

---

## Games

Games take the keyboard over while they run and hand it back on stop.

```bash
sinoctl game list
sinoctl game tetris start
sinoctl game stop
```

Declare one as a layer in its own profile to make it available:

```toml
[[profiles.tetris_game.layers]]
  type = "tetris"
  step_interval = 0.4
```

| Game | Controls |
| --- | --- |
| `snake` | Arrows steer; Enter/Space restarts after a crash |
| `tetris` | Up/Down move, Space rotates, Left hard-drops |
| `pong` | Up/Down move your paddle (the left column) |
| `life` | Press any key to toggle the cell under it |

Tetris runs sideways: the board is six rows tall and sixteen wide, far too
short for pieces to fall down it, so gravity runs along the long axis and a
full *column* is what clears.

Games need the `keycodes` CSV — that is how they read input.

---

## System-state layers

Layers driven by data instead of time.

```toml
[[profiles.sysmon.layers]]
  type = "system_meter"
  metric = "cpu"                    # cpu | memory | load | battery | <custom>
  bar_keys = ["F1", "F2", "F3", "F4", "F5", "F6"]
  blend = "add"
```

The bar fills `bar_keys` in the order listed. Meters paint their non-bar keys
black, so stack several with `blend = "add"` — otherwise each erases the one
below it.

Anything outside the daemon can drive a layer:

```toml
[[profiles.buildstatus.layers]]
  type = "status_light"
  signal = "build"
  keys = ["F1", "F2", "F3", "F4"]
  ok_timeout = 20.0
```

```bash
sinoctl state build busy     # amber sweep
make && sinoctl state build ok || sinoctl state build fail
sinoctl metric deploy 0.6    # feeds a system_meter with metric = "deploy"
```

`ok` fades out after `ok_timeout` so a green build does not stay lit all day.

---

## Architecture

- **`Runtime`** owns the model, the transport, the engine and the single render
  thread, and is the only place commands are dispatched from. Every frontend —
  the interactive CLI, the control socket, the window watchers — goes through
  it, so there is one lock protecting the engine and one thread touching the
  device. Lock order is documented in `runtime.hpp`: the loop mutex may be
  taken before the engine mutex, never the reverse.
- **`EffectEngine`** composites layers into a frame: render each layer into a
  scratch buffer, then blend it through the layer's key mask, opacity and blend
  mode. The render thread parks on a condition variable when nothing is
  animating, so a static profile costs no frames per second.
- **`LightingPreset`** is the effect interface. Add one by subclassing it,
  implementing `render`, and registering it in `buildRegistry()` in `main.cpp`.
  `GamePreset` extends it for effects that take the keyboard over.
- **`DeviceTransport`** abstracts the device: `hidapi` for real hardware,
  `preview` for the terminal, `logging` for hex dumps. The hidapi transport
  reconnects on its own — three consecutive write failures close the handle and
  it re-enumerates on a backoff, so an unplug or a suspend/resume cycle
  recovers without a restart.
- **`WindowSource`** abstracts focus tracking, with Hyprland, sway/i3 and X11
  backends chosen automatically.
- **`SystemState`** is the shared, cached source of `/proc` readings and of
  values pushed in over the socket.

### A note on KDE and GNOME

On Xorg they work through the X11 backend. On Wayland neither exposes a stable
active-window interface without a shell extension or a KWin script, so there is
no honest backend to ship. Drive `sinoctl profile` from your own key bindings
instead.

---

## Packet format

A 382-byte feature report: a 4-byte header, then RGB triplets for 96 keys, then
zero padding, sent with `hid_send_feature_report`.

```
vendor_id     = 0x258A
product_id    = 0x0049
packet_header = [0x08, 0x0A, 0x7A, 0x01]
packet_length = 382
```

The layout CSV lists keys in *packet* order, which on this keyboard runs down
the physical columns — 16 rows of 6 entries, where each CSV row is one physical
column of the board:

```
Esc,   Backtick, Tab,         Caps,       Shift, Ctrl
F1,    1,        Q,           A,          Z,     Win
F2,    2,        W,           S,          X,     Alt
...
Del,   Home,     End,         PgUp,       PgDn,  Right
```

A key's colour starts at byte `4 + index * 3`, where
`index = csv_row * 6 + csv_column`. `NAN` marks a position with no physical
key; it still occupies an index and is always sent as `00 00 00`.

The preview and the games both transpose this automatically, so "up" in a game
looks like up on the keyboard.

---

## Contributors

- @Evan (Lead Developer)
