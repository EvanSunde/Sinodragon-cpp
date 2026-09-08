# Chroma SDK shim

A stand-in `RzChromaSDK64.dll` for Chroma-integrated games running under Wine
or Proton. It answers the SDK calls itself and forwards the per-key frames a
game produces to a Chroma REST server on the host.

## Why it is needed

The real `RzChromaSDK64.dll` is an RPC client for the Razer Chroma SDK Service.
That service does not exist on Linux, so `Init()` fails, the game calls
`FreeLibrary` and quietly disables its lighting. In a `WINEDEBUG=+loaddll` trace
that looks like:

```
Loaded L"...\\RzChromaSDK64.dll" at 00006FFFFB9C0000: native
Unloaded module L"...\\RzChromaSDK64.dll" : native
```

Note that `WINEDEBUG=+relay` will *not* show `Init` or any other SDK call:
relay only instruments Wine's builtin modules, and a native PE's exports are
reached by direct jump. `GetProcAddress` is traceable (it lives in KERNEL32),
so grep for that to see which exports a game asks for.

## Which games need it

Games reach the SDK one of two ways, and only one of them needs this shim:

```bash
# Native DLL -- needs the shim.
strings -a "$GAME_DIR/Game.exe" | grep -i rzchromasdk

# REST -- talks to 127.0.0.1:54235 directly, the shim is not involved.
strings -a "$GAME_DIR/Game.exe" | grep -iE 'razer/chromasdk|54235'
```

## Build

Needs mingw-w64:

```bash
sudo pacman -S mingw-w64-gcc          # Arch
sudo apt install gcc-mingw-w64-x86-64  # Debian/Ubuntu

./build.sh
```

32-bit games need a 32-bit build named `RzChromaSDK.dll`:

```bash
CC=i686-w64-mingw32-gcc OUT=RzChromaSDK.dll ./build.sh
```

## Install

Drop the DLL next to the game's executable — the application directory comes
first in the Windows search order, so nothing else is required:

```bash
cp RzChromaSDK64.dll "$GAME_DIR/"
```

If the game loads the DLL by absolute path out of `system32` instead, put it
there and add an override so Wine prefers it:

```bash
cp RzChromaSDK64.dll "$WINEPREFIX/drive_c/windows/system32/"
export WINEDLLOVERRIDES="RzChromaSDK64=n"
```

For a Steam game, set the launch options to:

```
WINEDLLOVERRIDES="RzChromaSDK64=n" %command%
```

## Run

Start a Chroma REST server on the host first — `chroma_mock_server.py` logs
every request, which is what you want while working out what a game sends:

```bash
python3 chroma_mock_server.py
wine "$GAME_DIR/Game.exe"
```

Enable the game's Chroma option in its own settings; most ship with it off.

## Configuration

Read from the environment at load time:

| Variable | Default | Meaning |
| --- | --- | --- |
| `SINODRAGON_CHROMA_HOST` | `127.0.0.1` | REST server host |
| `SINODRAGON_CHROMA_PORT` | `54235` | REST server port |
| `SINODRAGON_CHROMA_LOG` | — | Log file; stderr when unset |
| `SINODRAGON_CHROMA_VERBOSE` | `0` | `1` logs every frame instead of every 300th |

## What it forwards

`CreateKeyboardEffect` is the path that matters. `CHROMA_CUSTOM` (2),
`CHROMA_CUSTOM_KEY` (8), `CHROMA_STATIC` (4) and `CHROMA_NONE` (0) become a
6x22 grid, sent as `PUT <session>/keyboard`:

```json
{"effect":"CHROMA_CUSTOM","param":[[255,65280,...22],...6 rows]}
```

Colours are `COLORREF`, which is **BGR**, not RGB:

```
r = c & 0xFF        g = (c >> 8) & 0xFF        b = (c >> 16) & 0xFF
```

Breathing, wave and spectrum-cycling carry no frame; they are logged and
otherwise ignored. Mouse, headset, mousepad, keypad and Chroma Link calls are
logged so it is visible when a game only lights those.

## Design notes

Games call `CreateKeyboardEffect` on their render thread at 30-60 Hz, so
nothing on that path may block. The exported functions copy the grid into a
single-slot buffer and return `RZRESULT_SUCCESS` immediately; a worker thread
does the HTTP. When the server is slower than the game, the worker sends the
newest frame and drops the ones it missed — stale lighting is worse than
skipped lighting.

`Init()` returns success even when no server is reachable. Reporting a failure
would make the game disable its lighting for the rest of the session, and the
server may well be started afterwards.

The worker is started from `Init()` rather than `DllMain`, which runs under the
loader lock.
