#!/usr/bin/env sh
# Builds the stand-in RzChromaSDK64.dll. Needs mingw-w64:
#   Arch:   sudo pacman -S mingw-w64-gcc
#   Debian: sudo apt install gcc-mingw-w64-x86-64
set -eu

cd "$(dirname "$0")"

CC=${CC:-x86_64-w64-mingw32-gcc}
OUT=${OUT:-RzChromaSDK64.dll}

"$CC" -shared -O2 -Wall -Wextra \
    -o "$OUT" chroma_shim.c \
    -lws2_32 \
    -static-libgcc \
    -Wl,--enable-stdcall-fixup

echo "built $OUT"

# The test harness: drives the shim the way a game does. Razer's own sample
# application cannot -- see README.
if [ "${SKIP_HARNESS:-0}" != "1" ]; then
    "$CC" -O2 -Wall -Wextra -o chroma_test.exe test_harness.c
    echo "built chroma_test.exe"
fi
