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
first in the Windows search order, so nothing else is required. (This is the
step Razer's sample application rejects; see above. Games do not.)

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

## Test it without a game

`build.sh` also produces `chroma_test.exe`, which drives the shim exactly as a
game does -- `LoadLibrary`, `GetProcAddress`, `Init`, a static frame, a marked
grid, then 120 frames at ~60 Hz:

```bash
python3 chroma_mock_server.py &
cp RzChromaSDK64.dll chroma_test.exe /tmp/chromatest/ && cd /tmp/chromatest
wine chroma_test.exe
```

The marked grid puts pure red, green and blue in the first three cells, so a
transposed grid or a BGR/RGB swap shows up immediately instead of looking
plausible. The frame burst reports its own rate; well under 60 fps means
something on the send path is blocking the caller.

### Why not Razer's sample application

`RazerChromaSampleApplication.exe` looks like the obvious test harness and is
useless as one. Before it resolves a single export it requires all of:

1. The loaded DLL's path (via `GetModuleFileNameExW`) to equal
   `%SystemRoot%\System32\RzChromaSDK64.dll` or
   `%ProgramFiles%\Razer Chroma SDK\bin\RzChromaSDK64.dll` -- a DLL beside
   the .exe is rejected.
2. `WinVerifyTrust(WINTRUST_ACTION_GENERIC_VERIFY_V2)` to pass on it, i.e. a
   valid Authenticode signature.
3. `HKLM\SOFTWARE\RAZER CHROMA SDK\InstallPath` to name an `RzSDKService.exe`
   whose signer matches the DLL's.

Only then does it `GetProcAddress` for `InitSDK`, `CreateEffect`,
`CreateKeyboardEffect`, `CreateMouseEffect`, `CreateMousepadEffect`,
`CreateKeypadEffect`, `CreateHeadsetEffect`, `CreateChromaLinkEffect`,
`SetEffect` and `DeleteEffect`, and call `InitSDK`.

A replacement DLL fails 1 and 2, so the app unloads it without calling
anything -- which is what a `LoadLibraryW` immediately followed by
`FreeLibrary`, with no `GetProcAddress` between them, means in a relay log.
The stock Razer DLL is rejected the same way when it is not installed in one
of those two directories.

This is the sample application demonstrating Razer's recommended "check the
SDK is genuine" pattern. Games have no reason to do it, but it is worth
confirming for a specific title before assuming the shim will be used:

```bash
objdump -p Game.exe | grep -i wintrust
strings -el Game.exe | grep -iE 'Razer Chroma SDK.bin|System32.RzChroma'
```

Anything found there means that game validates the SDK too, and the shim will
not be loaded by it.

## Run

Start a Chroma REST server on the host first — `chroma_mock_server.py` logs
every request, which is what you want while working out what a game sends:

```bash
python3 chroma_mock_server.py
wine "$GAME_DIR/Game.exe"
```

Enable the game's Chroma option in its own settings; most ship with it off.

## Troubleshooting

**`BrokenPipeError` on the server, once per heartbeat and per frame.** Fixed in
the shim as of the drain-the-reply change: it used to close each connection
without reading the response, and a socket closed with unread data makes the
stack send an RST rather than a FIN, so the server's write failed. Rebuild the
DLL. `chroma_mock_server.py` also tolerates it now, so an older shim no longer
produces a traceback per frame.

**The game freezes at a moment that closes the lighting session** (a death, a
reload, a level transition). Fixed by making the worker thread live for the
process rather than for one session: `UnInit` used to join it, on the game's
own thread, which stalled the game for as long as the worker took to notice.
`Init` and `UnInit` now only flip a flag and return at once. `chroma_test.exe`
times three close/reopen cycles and fails if any call holds the caller for more
than 100 ms.

**Heartbeats but no frames.** Registration succeeded and the game is holding
the session open, but it is not drawing. Most games only light up during play,
not in menus, and many ship with Chroma off by default. Check the shim's own
log (stderr, or `SINODRAGON_CHROMA_LOG`) — it reports frame counts, and logs
every effect type it cannot forward, so a game asking only for `CHROMA_WAVE`
or lighting a device other than the keyboard is visible there.

## Configuration

Read from the environment at load time:

| Variable | Default | Meaning |
| --- | --- | --- |
| `SINODRAGON_CHROMA_HOST` | `127.0.0.1` | REST server host |
| `SINODRAGON_CHROMA_PORT` | `54235` | REST server port |
| `SINODRAGON_CHROMA_LOG` | — | Log file; stderr when unset |
| `SINODRAGON_CHROMA_VERBOSE` | `0` | `1` logs every frame instead of every 300th |

## What a game actually sends

Dead Cells, mid-run, sends `CHROMA_CUSTOM` grids at roughly 15 Hz: a `#FF005A`
background with a green bar along row 1 (the number row) whose length tracks
health. So the semantic state is there, but only as pixels -- the count of lit
cells in that row is the health reading, and there is no other channel carrying
it. That is the shape of every Chroma integration, and the reason a sink layer
composites frames rather than interpreting them.

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
