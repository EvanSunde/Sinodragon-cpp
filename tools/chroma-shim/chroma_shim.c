// A stand-in RzChromaSDK64.dll for games running under Wine or Proton.
//
// The real DLL is an RPC client for the Razer Chroma SDK Service, which does
// not exist on Linux: Init() fails, the game unloads the DLL and quietly
// disables its lighting. This replacement answers every call itself and
// forwards the per-key frames a game produces to a Chroma REST server on the
// host -- tools/chroma_mock_server.py, or anything else listening on 54235.
//
// Games call CreateKeyboardEffect on their render thread at 30-60 Hz, so
// nothing here may block: the exported functions copy the grid into a
// single-slot buffer and return immediately, and a worker thread does the
// HTTP. When the server is slow the worker drops the frames it missed rather
// than queueing them, because stale lighting is worse than skipped lighting.
//
// Build with build.sh (x86_64-w64-mingw32-gcc).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------- SDK types

typedef LONG RZRESULT;
typedef GUID RZDEVICEID;
typedef GUID RZEFFECTID;

#define RZRESULT_SUCCESS 0
#define RZRESULT_INVALID_PARAMETER 87

// ChromaSDK::Keyboard::EFFECT_TYPE. Only CUSTOM and CUSTOM_KEY carry a grid;
// the rest are whole-device effects the daemon has better versions of.
#define KB_EFFECT_NONE 0
#define KB_EFFECT_CUSTOM 2
#define KB_EFFECT_STATIC 4
#define KB_EFFECT_CUSTOM_KEY 8

// ChromaSDK::EFFECT_TYPE, the device-agnostic enum used by CreateEffect. It is
// numbered differently from the keyboard one above -- a frequent source of
// confusion when reading the SDK headers.
#define GENERIC_EFFECT_STATIC 6
#define GENERIC_EFFECT_CUSTOM 7

#define KB_ROWS 6
#define KB_COLS 22

// Keyboard CUSTOM_KEY payload: Color is the grid, Key overrides it for
// individual keys. A Key entry counts as set when any of its high bits are on,
// which is how the SDK distinguishes "addressed" from "left alone".
typedef struct {
    COLORREF Color[KB_ROWS][KB_COLS];
    COLORREF Key[KB_ROWS][KB_COLS];
} KB_CUSTOM_KEY_EFFECT;

typedef struct {
    COLORREF Color[KB_ROWS][KB_COLS];
} KB_CUSTOM_EFFECT;

typedef struct {
    COLORREF Color;
} KB_STATIC_EFFECT;

// ChromaSDK::DEVICE_INFO_TYPE. Games that call QueryDevice before doing
// anything else give up when Connected is zero, so we always claim one.
typedef struct {
    int DeviceType;
    DWORD Connected;
} DEVICE_INFO_TYPE;

#define DEVICE_KEYBOARD 1

// ------------------------------------------------------------------- config

static char g_host[128] = "127.0.0.1";
static int g_port = 54235;
static int g_verbose = 0;
static FILE* g_log = NULL;

// Session path handed back by the server's registration reply, e.g.
// "/razer/chromasdk/1234567890". Empty until Init() has registered.
static char g_session[256] = "";

static void shim_log(const char* fmt, ...) {
    if (g_log == NULL) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    fprintf(g_log, "[chroma-shim] ");
    vfprintf(g_log, fmt, args);
    fprintf(g_log, "\n");
    va_end(args);
    fflush(g_log);
}

static void load_config(void) {
    const char* host = getenv("SINODRAGON_CHROMA_HOST");
    if (host != NULL && *host != '\0') {
        strncpy(g_host, host, sizeof(g_host) - 1);
        g_host[sizeof(g_host) - 1] = '\0';
    }
    const char* port = getenv("SINODRAGON_CHROMA_PORT");
    if (port != NULL && *port != '\0') {
        const int parsed = atoi(port);
        if (parsed > 0 && parsed < 65536) {
            g_port = parsed;
        }
    }
    const char* verbose = getenv("SINODRAGON_CHROMA_VERBOSE");
    g_verbose = (verbose != NULL && *verbose == '1');

    // Under Wine stderr lands in the terminal that launched the game, which is
    // where you want this while working out what a game sends.
    const char* path = getenv("SINODRAGON_CHROMA_LOG");
    if (path != NULL && *path != '\0') {
        g_log = fopen(path, "a");
    }
    if (g_log == NULL) {
        g_log = stderr;
    }
}

// -------------------------------------------------------------- frame queue

static CRITICAL_SECTION g_lock;
static COLORREF g_frame[KB_ROWS][KB_COLS];
static BOOL g_frame_pending = FALSE;
static HANDLE g_frame_event = NULL;
static HANDLE g_worker = NULL;
static volatile LONG g_running = 0;
static volatile LONG g_frames_in = 0;
static volatile LONG g_frames_sent = 0;

// ---------------------------------------------------------------- transport

// One connection per request. At 30 Hz over loopback that is cheap, and it
// keeps us correct when the server closes an idle connection between frames.
static SOCKET http_connect(void) {
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    char port_text[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    snprintf(port_text, sizeof(port_text), "%d", g_port);

    if (getaddrinfo(g_host, port_text, &hints, &result) != 0) {
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock == INVALID_SOCKET) {
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }

    // Without a timeout a wedged server would stall the worker forever, and
    // the frame slot would stop draining.
    DWORD timeout_ms = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    if (connect(sock, result->ai_addr, (int)result->ai_addrlen) != 0) {
        closesocket(sock);
        freeaddrinfo(result);
        return INVALID_SOCKET;
    }
    freeaddrinfo(result);
    return sock;
}

static BOOL send_all(SOCKET sock, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        const int n = send(sock, data + sent, length - sent, 0);
        if (n <= 0) {
            return FALSE;
        }
        sent += n;
    }
    return TRUE;
}

// Sends one request and reads the reply into `reply` (may be NULL). Returns
// FALSE when the server could not be reached, which callers treat as "carry on
// without lighting" rather than an error to report to the game.
static BOOL http_request(const char* method, const char* path, const char* body,
                         char* reply, size_t reply_size) {
    SOCKET sock = http_connect();
    if (sock == INVALID_SOCKET) {
        return FALSE;
    }

    char header[512];
    const int body_length = (body != NULL) ? (int)strlen(body) : 0;
    const int header_length = snprintf(header, sizeof(header),
                                       "%s %s HTTP/1.1\r\n"
                                       "Host: %s:%d\r\n"
                                       "Content-Type: application/json\r\n"
                                       "Content-Length: %d\r\n"
                                       "Connection: close\r\n"
                                       "\r\n",
                                       method, path, g_host, g_port, body_length);

    BOOL ok = (header_length > 0) && (header_length < (int)sizeof(header)) &&
              send_all(sock, header, header_length);
    if (ok && body_length > 0) {
        ok = send_all(sock, body, body_length);
    }

    if (ok && reply != NULL && reply_size > 0) {
        size_t total = 0;
        while (total + 1 < reply_size) {
            const int n = recv(sock, reply + total, (int)(reply_size - total - 1), 0);
            if (n <= 0) {
                break;
            }
            total += (size_t)n;
        }
        reply[total] = '\0';
    } else if (reply != NULL && reply_size > 0) {
        reply[0] = '\0';
    }

    closesocket(sock);
    return ok;
}

// Pulls the path out of the "uri" the server returns at registration. Real
// Synapse answers with a different port per session; we keep the path and stay
// on the configured port, which is what a local mock server expects.
static void adopt_session_uri(const char* reply) {
    const char* uri = strstr(reply, "\"uri\"");
    if (uri == NULL) {
        return;
    }
    // uri + 5 is already past the "uri" key, so the next quote opens the value.
    const char* open_quote = strchr(uri + 5, '"');
    if (open_quote == NULL) {
        return;
    }
    const char* value = open_quote + 1;
    const char* close_quote = strchr(value, '"');
    if (close_quote == NULL) {
        return;
    }

    // Skip "http://host:port", keeping everything from the path onwards.
    const char* path = value;
    const char* scheme = strstr(value, "://");
    if (scheme != NULL && scheme < close_quote) {
        path = strchr(scheme + 3, '/');
        if (path == NULL || path >= close_quote) {
            return;
        }
    }

    const size_t length = (size_t)(close_quote - path);
    if (length == 0 || length >= sizeof(g_session)) {
        return;
    }
    memcpy(g_session, path, length);
    g_session[length] = '\0';
}

// ------------------------------------------------------------------- worker

static void post_frame(COLORREF grid[KB_ROWS][KB_COLS]) {
    // 132 colours of at most 10 digits, 131 separators, 14 brackets and a ~35
    // byte header: a shade over 1500 bytes, so this cannot truncate and the
    // running `used` offset stays inside the buffer.
    char body[4096];
    int used = snprintf(body, sizeof(body), "{\"effect\":\"CHROMA_CUSTOM\",\"param\":[");
    for (int row = 0; row < KB_ROWS; ++row) {
        used += snprintf(body + used, sizeof(body) - (size_t)used, "%s[", row ? "," : "");
        for (int col = 0; col < KB_COLS; ++col) {
            used += snprintf(body + used, sizeof(body) - (size_t)used, "%s%lu", col ? "," : "",
                             (unsigned long)grid[row][col]);
        }
        used += snprintf(body + used, sizeof(body) - (size_t)used, "]");
    }
    snprintf(body + used, sizeof(body) - (size_t)used, "]}");

    char path[320];
    snprintf(path, sizeof(path), "%s/keyboard", g_session[0] ? g_session : "/razer/chromasdk");
    if (http_request("PUT", path, body, NULL, 0)) {
        InterlockedIncrement(&g_frames_sent);
    }
}

static DWORD WINAPI worker_main(LPVOID unused) {
    (void)unused;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        shim_log("WSAStartup failed; lighting will not be forwarded");
        return 0;
    }

    // Register with the server. A failure here is not fatal: the game keeps
    // running, and the next frame retries against the default path.
    char reply[1024];
    const char* app_info =
        "{\"title\":\"sinodragon-chroma-shim\","
        "\"description\":\"Chroma SDK bridge for Wine\","
        "\"author\":{\"name\":\"sinodragon\",\"contact\":\"\"},"
        "\"device_supported\":[\"keyboard\"],"
        "\"category\":\"application\"}";
    if (http_request("POST", "/razer/chromasdk", app_info, reply, sizeof(reply))) {
        adopt_session_uri(reply);
        shim_log("registered, session path %s", g_session[0] ? g_session : "(default)");
    } else {
        shim_log("no Chroma server on %s:%d -- frames will be dropped until one appears", g_host,
                 g_port);
    }

    DWORD last_heartbeat = GetTickCount();
    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        // A 1 s wait doubles as the heartbeat tick when no frames arrive.
        WaitForSingleObject(g_frame_event, 1000);

        COLORREF grid[KB_ROWS][KB_COLS];
        BOOL have_frame = FALSE;
        EnterCriticalSection(&g_lock);
        if (g_frame_pending) {
            memcpy(grid, g_frame, sizeof(grid));
            g_frame_pending = FALSE;
            have_frame = TRUE;
        }
        LeaveCriticalSection(&g_lock);

        if (have_frame) {
            post_frame(grid);
        }

        const DWORD now = GetTickCount();
        if (now - last_heartbeat >= 1000 && g_session[0] != '\0') {
            char path[320];
            snprintf(path, sizeof(path), "%s/heartbeat", g_session);
            http_request("POST", path, "{}", NULL, 0);
            last_heartbeat = now;
        }
    }

    if (g_session[0] != '\0') {
        http_request("DELETE", g_session, NULL, NULL, 0);
    }
    WSACleanup();
    return 0;
}

static void start_worker(void) {
    if (InterlockedCompareExchange(&g_running, 1, 0) != 0) {
        return;  // Already running.
    }
    g_frame_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    g_worker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
    if (g_worker == NULL) {
        InterlockedExchange(&g_running, 0);
        shim_log("could not start the worker thread");
    }
}

static void stop_worker(void) {
    if (InterlockedCompareExchange(&g_running, 0, 1) != 1) {
        return;
    }
    if (g_frame_event != NULL) {
        SetEvent(g_frame_event);
    }
    if (g_worker != NULL) {
        WaitForSingleObject(g_worker, 3000);
        CloseHandle(g_worker);
        g_worker = NULL;
    }
    if (g_frame_event != NULL) {
        CloseHandle(g_frame_event);
        g_frame_event = NULL;
    }
}

// Replaces whatever the worker had not sent yet. Dropping the previous frame is
// deliberate -- see the note at the top of the file.
static void submit_frame(COLORREF grid[KB_ROWS][KB_COLS]) {
    EnterCriticalSection(&g_lock);
    memcpy(g_frame, grid, sizeof(g_frame));
    g_frame_pending = TRUE;
    LeaveCriticalSection(&g_lock);

    if (g_frame_event != NULL) {
        SetEvent(g_frame_event);
    }

    const LONG count = InterlockedIncrement(&g_frames_in);
    if (g_verbose) {
        shim_log("frame %ld", count);
    } else if (count == 1 || (count % 300) == 0) {
        shim_log("frame %ld (%ld forwarded)", count, InterlockedCompareExchange(&g_frames_sent, 0, 0));
    }
}

static void fill_uniform(COLORREF grid[KB_ROWS][KB_COLS], COLORREF color) {
    for (int row = 0; row < KB_ROWS; ++row) {
        for (int col = 0; col < KB_COLS; ++col) {
            grid[row][col] = color;
        }
    }
}

// ------------------------------------------------------------------ exports

__declspec(dllexport) RZRESULT Init(void) {
    shim_log("Init");
    start_worker();
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT InitSDK(void* app_info) {
    (void)app_info;
    shim_log("InitSDK");
    start_worker();
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT UnInit(void) {
    shim_log("UnInit (%ld frames in, %ld forwarded)",
             InterlockedCompareExchange(&g_frames_in, 0, 0),
             InterlockedCompareExchange(&g_frames_sent, 0, 0));
    stop_worker();
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateKeyboardEffect(int effect, void* param, RZEFFECTID* effect_id) {
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }

    COLORREF grid[KB_ROWS][KB_COLS];

    switch (effect) {
        case KB_EFFECT_CUSTOM: {
            if (param == NULL) {
                return RZRESULT_INVALID_PARAMETER;
            }
            memcpy(grid, ((KB_CUSTOM_EFFECT*)param)->Color, sizeof(grid));
            break;
        }
        case KB_EFFECT_CUSTOM_KEY: {
            if (param == NULL) {
                return RZRESULT_INVALID_PARAMETER;
            }
            const KB_CUSTOM_KEY_EFFECT* custom = (const KB_CUSTOM_KEY_EFFECT*)param;
            for (int row = 0; row < KB_ROWS; ++row) {
                for (int col = 0; col < KB_COLS; ++col) {
                    // A Key entry with its high byte set is an addressed key
                    // and wins over the background grid.
                    const COLORREF key = custom->Key[row][col];
                    grid[row][col] = (key & 0xFF000000u) ? (key & 0x00FFFFFFu) : custom->Color[row][col];
                }
            }
            break;
        }
        case KB_EFFECT_STATIC: {
            if (param == NULL) {
                return RZRESULT_INVALID_PARAMETER;
            }
            fill_uniform(grid, ((KB_STATIC_EFFECT*)param)->Color);
            break;
        }
        case KB_EFFECT_NONE: {
            fill_uniform(grid, 0);
            break;
        }
        default: {
            // Breathing, wave, spectrum cycling and friends are whole-device
            // animations with no frame to forward. Logged so it is obvious when
            // a game only ever asks for these.
            shim_log("CreateKeyboardEffect: unhandled effect type %d", effect);
            return RZRESULT_SUCCESS;
        }
    }

    submit_frame(grid);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateEffect(RZDEVICEID device_id, int effect, void* param,
                                            RZEFFECTID* effect_id) {
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateEffect: device %08lx-%04x-%04x effect %d",
             (unsigned long)device_id.Data1, device_id.Data2, device_id.Data3, effect);

    // The generic entry point carries the same 6x22 grid for a keyboard, but
    // the device GUID is the only clue about which device it is for, and
    // mapping every Razer GUID is not worth it. Forwarding a custom payload
    // works when the game drives a keyboard and is harmless otherwise, since a
    // frame for another device just paints one more grid we then overwrite.
    if (effect == GENERIC_EFFECT_CUSTOM && param != NULL) {
        COLORREF grid[KB_ROWS][KB_COLS];
        memcpy(grid, param, sizeof(grid));
        submit_frame(grid);
    }
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateMouseEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateMouseEffect: effect %d", effect);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateHeadsetEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateHeadsetEffect: effect %d", effect);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateMousepadEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateMousepadEffect: effect %d", effect);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateKeypadEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateKeypadEffect: effect %d", effect);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateChromaLinkEffect(int effect, void* param,
                                                      RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        memset(effect_id, 0, sizeof(*effect_id));
    }
    shim_log("CreateChromaLinkEffect: effect %d", effect);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT SetEffect(RZEFFECTID effect_id) {
    // Frames are forwarded as they are created, so the two-phase
    // create-then-set path needs nothing here beyond not failing.
    (void)effect_id;
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT DeleteEffect(RZEFFECTID effect_id) {
    (void)effect_id;
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT RegisterEventNotification(HWND window) {
    (void)window;
    shim_log("RegisterEventNotification");
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT UnregisterEventNotification(void) {
    shim_log("UnregisterEventNotification");
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT QueryDevice(RZDEVICEID device_id, DEVICE_INFO_TYPE* device_info) {
    (void)device_id;
    if (device_info == NULL) {
        return RZRESULT_INVALID_PARAMETER;
    }
    // Games that probe first and light up second give up on a disconnected
    // device, so claim a connected keyboard whatever was asked about.
    device_info->DeviceType = DEVICE_KEYBOARD;
    device_info->Connected = 1;
    return RZRESULT_SUCCESS;
}

// ------------------------------------------------------------------ DllMain

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            InitializeCriticalSection(&g_lock);
            load_config();
            shim_log("loaded, forwarding to %s:%d", g_host, g_port);
            // No thread or socket work here: DllMain runs under the loader
            // lock, and a worker started from it cannot run until we return.
            break;
        case DLL_PROCESS_DETACH:
            stop_worker();
            DeleteCriticalSection(&g_lock);
            break;
        default:
            break;
    }
    return TRUE;
}
