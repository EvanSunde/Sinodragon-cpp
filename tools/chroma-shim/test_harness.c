// Drives the shim the way a game does, so it can be tested without one.
//
// Razer's own RazerChromaSampleApplication cannot do this job: before it
// resolves a single export it requires the DLL to sit in System32 or in
// %ProgramFiles%\Razer Chroma SDK\bin, to carry a valid Authenticode
// signature, and to be signed by the same publisher as the RzSDKService.exe
// named in HKLM\SOFTWARE\RAZER CHROMA SDK\InstallPath. A replacement DLL fails
// all three, and is unloaded without Init ever being called.
//
// This harness does what a game actually does -- LoadLibrary, GetProcAddress,
// Init, a few frames, UnInit -- and nothing else.
//
//   x86_64-w64-mingw32-gcc -O2 -o chroma_test.exe test_harness.c
//   wine chroma_test.exe

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define KB_ROWS 6
#define KB_COLS 22

#define CHROMA_CUSTOM 2
#define CHROMA_STATIC 4
#define CHROMA_CUSTOM_KEY 8

typedef LONG RZRESULT;
typedef GUID RZEFFECTID;

typedef RZRESULT(*INIT_FN)(void);
typedef RZRESULT(*UNINIT_FN)(void);
typedef RZRESULT(*CREATE_KEYBOARD_FN)(int, void*, RZEFFECTID*);
typedef RZRESULT(*SET_EFFECT_FN)(RZEFFECTID);
typedef RZRESULT(*CREATE_EFFECT_FN)(GUID, int, void*, RZEFFECTID*);
typedef RZRESULT(*DELETE_EFFECT_FN)(RZEFFECTID);

typedef struct {
    COLORREF Color[KB_ROWS][KB_COLS];
} KB_CUSTOM_EFFECT;

typedef struct {
    COLORREF Color;
} KB_STATIC_EFFECT;

// COLORREF is BGR, not RGB -- the single easiest thing to get backwards.
static COLORREF rgb(int r, int g, int b) {
    return (COLORREF)((b << 16) | (g << 8) | r);
}

int main(void) {
    HMODULE module = LoadLibraryA("RzChromaSDK64.dll");
    if (module == NULL) {
        printf("FAIL  LoadLibrary: error %lu\n", GetLastError());
        printf("      Put RzChromaSDK64.dll next to this .exe.\n");
        return 1;
    }
    printf("ok    LoadLibrary\n");

    // Games use one or the other, so accept either.
    INIT_FN init = (INIT_FN)(void*)GetProcAddress(module, "Init");
    if (init == NULL) {
        init = (INIT_FN)(void*)GetProcAddress(module, "InitSDK");
    }
    CREATE_KEYBOARD_FN create = (CREATE_KEYBOARD_FN)(void*)GetProcAddress(module, "CreateKeyboardEffect");
    UNINIT_FN uninit = (UNINIT_FN)(void*)GetProcAddress(module, "UnInit");
    SET_EFFECT_FN set_effect = (SET_EFFECT_FN)(void*)GetProcAddress(module, "SetEffect");
    DELETE_EFFECT_FN delete_effect = (DELETE_EFFECT_FN)(void*)GetProcAddress(module, "DeleteEffect");
    CREATE_EFFECT_FN create_effect = (CREATE_EFFECT_FN)(void*)GetProcAddress(module, "CreateEffect");

    if (init == NULL || create == NULL || uninit == NULL) {
        printf("FAIL  GetProcAddress: Init=%p CreateKeyboardEffect=%p UnInit=%p\n",
               (void*)init, (void*)create, (void*)uninit);
        FreeLibrary(module);
        return 1;
    }
    printf("ok    GetProcAddress (Init, CreateKeyboardEffect, UnInit)\n");

    const RZRESULT init_result = init();
    printf("%s Init returned %ld\n", init_result == 0 ? "ok   " : "FAIL ", (long)init_result);
    if (init_result != 0) {
        FreeLibrary(module);
        return 1;
    }

    // A static frame first: every key one colour, easy to confirm by eye.
    KB_STATIC_EFFECT solid;
    solid.Color = rgb(0, 0, 255);
    RZRESULT result = create(CHROMA_STATIC, &solid, NULL);
    printf("%s CHROMA_STATIC (all blue) returned %ld\n", result == 0 ? "ok   " : "FAIL ", (long)result);

    Sleep(500);

    // Then a marked grid, so a wrong row/column order or a BGR swap is obvious
    // rather than plausible: red, green and blue in the first three cells and a
    // dim ramp across the rest.
    KB_CUSTOM_EFFECT custom;
    for (int row = 0; row < KB_ROWS; ++row) {
        for (int col = 0; col < KB_COLS; ++col) {
            custom.Color[row][col] = rgb(col * 10, row * 40, 0);
        }
    }
    custom.Color[0][0] = rgb(255, 0, 0);
    custom.Color[0][1] = rgb(0, 255, 0);
    custom.Color[0][2] = rgb(0, 0, 255);

    result = create(CHROMA_CUSTOM, &custom, NULL);
    printf("%s CHROMA_CUSTOM (marked grid) returned %ld\n", result == 0 ? "ok   " : "FAIL ", (long)result);

    // Finally a burst at frame rate, which is where a blocking transport would
    // show up as a stall.
    const DWORD started = GetTickCount();
    for (int frame = 0; frame < 120; ++frame) {
        for (int row = 0; row < KB_ROWS; ++row) {
            for (int col = 0; col < KB_COLS; ++col) {
                const int phase = (col + frame) % KB_COLS;
                custom.Color[row][col] = rgb(phase * 11, 255 - phase * 11, 60);
            }
        }
        create(CHROMA_CUSTOM, &custom, NULL);
        Sleep(16);
    }
    const DWORD elapsed = GetTickCount() - started;
    printf("ok    120 frames in %lu ms (%.1f fps, %lu ms/frame)\n", elapsed,
           elapsed ? 120000.0 / (double)elapsed : 0.0, elapsed / 120);
    if (elapsed > 4000) {
        printf("WARN  that is far slower than the 16 ms sleep implies; the\n");
        printf("      transport is blocking the caller.\n");
    }

    // Create-now-show-later, the SDK's second mode. Passing an effect id means
    // "build this, I will SetEffect it when I want it" -- games pre-build their
    // ambient and flash effects this way. Each step is one solid colour so the
    // order the keyboard shows them in says whether it was honoured:
    //   correct:   red, blue, green      (green only once SetEffect asks)
    //   broken:    red, green, blue      (green shown at creation, never after)
    if (set_effect != NULL && delete_effect != NULL) {
        KB_STATIC_EFFECT red = {rgb(255, 0, 0)};
        KB_STATIC_EFFECT green = {rgb(0, 255, 0)};
        KB_STATIC_EFFECT blue = {rgb(0, 0, 255)};
        RZEFFECTID later;
        memset(&later, 0, sizeof(later));

        create(CHROMA_STATIC, &red, NULL);
        Sleep(400);
        create(CHROMA_STATIC, &green, &later);
        Sleep(400);
        create(CHROMA_STATIC, &blue, NULL);
        Sleep(400);
        const RZRESULT shown = set_effect(later);
        Sleep(400);
        delete_effect(later);
        const RZRESULT stale = set_effect(later);
        Sleep(400);

        static const GUID zero;
        printf("%s created effect got a real id\n", memcmp(&later, &zero, sizeof(zero)) ? "ok   " : "FAIL ");
        printf("%s SetEffect on it returned %ld\n", shown == 0 ? "ok   " : "FAIL ", (long)shown);
        // Success on purpose: an error here could make a game give up on
        // lighting. That it changes nothing shows in the colour order below.
        printf("%s SetEffect after DeleteEffect returned %ld and should change nothing\n",
               stale == 0 ? "ok   " : "FAIL ", (long)stale);
        printf("      expect the keyboard to show red, blue, green -- in that order\n");
    }

    // The generic CreateEffect names a device by GUID. A mouse's custom grid is
    // 9x7 -- far smaller than a keyboard's -- so treating it as a keyboard frame
    // would read past the game's buffer and paint garbage on the keys. Only the
    // keyboard one may show:
    //   correct:   ...green, magenta     (the mouse effect never appears)
    if (create_effect != NULL) {
        const GUID mouse = {0xAEC50D91, 0xB1F1, 0x452F, {0x8E, 0x16, 0x7B, 0x73, 0xF3, 0x76, 0xFD, 0xF3}};
        const GUID blackwidow = {0x2EA1BB63, 0xCA28, 0x428D, {0x9F, 0x06, 0x19, 0x6B, 0x88, 0x33, 0x0B, 0xBB}};
        COLORREF mouse_grid[9][7];
        for (int r = 0; r < 9; ++r) {
            for (int c = 0; c < 7; ++c) {
                mouse_grid[r][c] = rgb(255, 255, 0);  // yellow: must never reach the keys
            }
        }
        create_effect(mouse, 7 /* CHROMA_CUSTOM */, mouse_grid, NULL);
        Sleep(400);
        KB_CUSTOM_EFFECT magenta;
        for (int r = 0; r < KB_ROWS; ++r) {
            for (int c = 0; c < KB_COLS; ++c) {
                magenta.Color[r][c] = rgb(255, 0, 255);
            }
        }
        create_effect(blackwidow, 7 /* CHROMA_CUSTOM */, &magenta, NULL);
        Sleep(400);
        printf("      expect magenta next, and never yellow (a mouse effect)\n");
    }

    // The death-and-reload cycle. Dead Cells closes and reopens its Chroma
    // session when a run ends, and UnInit runs on the game's own thread -- so
    // if it blocks, the game visibly freezes at exactly that moment. Time it.
    int slowest = 0;
    for (int cycle = 0; cycle < 3; ++cycle) {
        DWORD at = GetTickCount();
        uninit();
        const int uninit_ms = (int)(GetTickCount() - at);

        at = GetTickCount();
        init();
        const int init_ms = (int)(GetTickCount() - at);

        for (int frame = 0; frame < 10; ++frame) {
            create(CHROMA_CUSTOM, &custom, NULL);
            Sleep(16);
        }
        printf("ok    cycle %d: UnInit %d ms, Init %d ms\n", cycle + 1, uninit_ms, init_ms);
        if (uninit_ms > slowest) slowest = uninit_ms;
        if (init_ms > slowest) slowest = init_ms;
    }

    if (slowest > 100) {
        printf("FAIL  %d ms on the caller's thread -- a game would freeze here\n", slowest);
    } else {
        printf("ok    no cycle stalled the caller (worst %d ms)\n", slowest);
    }

    result = uninit();
    printf("%s UnInit returned %ld\n", result == 0 ? "ok   " : "FAIL ", (long)result);

    FreeLibrary(module);
    printf("\ndone -- check the server log for the frames and session churn.\n");
    return slowest > 100 ? 1 : 0;
}
