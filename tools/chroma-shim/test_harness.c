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

    result = uninit();
    printf("%s UnInit returned %ld\n", result == 0 ? "ok   " : "FAIL ", (long)result);

    FreeLibrary(module);
    printf("\ndone -- check the server log for 122 frames.\n");
    return 0;
}
