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
static volatile LONG g_running = 0;        // Worker alive; process lifetime.
static volatile LONG g_session_active = 0;  // Between the game's Init and UnInit.
static volatile LONG g_frames_in = 0;
static volatile LONG g_frames_sent = 0;

// Everything a game's own thread wants to report goes into a counter here, and
// the worker turns it into log lines. Writing to the log from an exported
// function would put an fflush -- a blocking pipe write, under Wine -- in the
// middle of the game's render thread, and a terminal that is slow to drain
// would then show up as the game stuttering or hanging.
enum {
    CALL_MOUSE, CALL_HEADSET, CALL_MOUSEPAD, CALL_KEYPAD, CALL_CHROMALINK,
    CALL_GENERIC, CALL_KIND_COUNT
};
static const char* const kCallNames[CALL_KIND_COUNT] = {
    "mouse", "headset", "mousepad", "keypad", "chromalink", "CreateEffect"
};
static volatile LONG g_calls[CALL_KIND_COUNT];
static volatile LONG g_unhandled_effect = -1;

// ---------------------------------------------------------------- transport

static struct sockaddr_in g_server_addr;
static BOOL g_server_addr_valid = FALSE;

// Resolved once, when the worker starts. Calling getaddrinfo per request would
// put a name lookup between every frame and the wire, and under Wine that
// reaches the host resolver.
static BOOL resolve_server(void) {
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    char port_text[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICHOST;  // The default really is an address.
    snprintf(port_text, sizeof(port_text), "%d", g_port);

    if (getaddrinfo(g_host, port_text, &hints, &result) != 0) {
        hints.ai_flags = 0;  // Someone pointed us at a hostname after all.
        if (getaddrinfo(g_host, port_text, &hints, &result) != 0) {
            return FALSE;
        }
    }
    memcpy(&g_server_addr, result->ai_addr, sizeof(g_server_addr));
    freeaddrinfo(result);
    g_server_addr_valid = TRUE;
    return TRUE;
}

// One connection per request. At 30 Hz over loopback that is cheap, and it
// keeps us correct when the server closes an idle connection between frames.
static SOCKET http_connect(void) {
    if (!g_server_addr_valid) {
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    // Without a timeout a wedged server would stall the worker forever, and
    // the frame slot would stop draining.
    DWORD timeout_ms = 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms));

    if (connect(sock, (const struct sockaddr*)&g_server_addr, sizeof(g_server_addr)) != 0) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
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

    // Always drain the reply, even when the caller does not want it. Closing a
    // socket that still has unread data makes the stack send an RST rather than
    // a FIN, and the server then fails to write its response -- which shows up
    // at the far end as a broken pipe on every heartbeat and every frame.
    char discard[512];
    size_t total = 0;
    for (;;) {
        const BOOL keeping = (reply != NULL) && (total + 1 < reply_size);
        char* const dst = keeping ? reply + total : discard;
        const size_t capacity = keeping ? (reply_size - total - 1) : sizeof(discard);

        const int n = recv(sock, dst, (int)capacity, 0);
        if (n <= 0) {
            break;  // 0 is the orderly close we asked for with Connection: close.
        }
        if (keeping) {
            total += (size_t)n;
        }
    }
    if (reply != NULL && reply_size > 0) {
        reply[total] = '\0';
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

static void report_progress(void);  // Below, next to what it reports.

static DWORD WINAPI worker_main(LPVOID unused) {
    (void)unused;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        shim_log("WSAStartup failed; lighting will not be forwarded");
        return 0;
    }
    if (!resolve_server()) {
        shim_log("cannot resolve %s:%d; lighting will not be forwarded", g_host, g_port);
        WSACleanup();
        return 0;
    }

    BOOL registered = FALSE;
    DWORD last_heartbeat = GetTickCount();
    DWORD last_report = last_heartbeat;

    while (InterlockedCompareExchange(&g_running, 1, 1) == 1) {
        // A 1 s wait doubles as the heartbeat tick when no frames arrive.
        WaitForSingleObject(g_frame_event, 1000);

        // A game opens and closes its session more than once -- Dead Cells does
        // it on every death and reload -- so follow the flag rather than tying
        // the session to this thread's lifetime.
        const BOOL active = InterlockedCompareExchange(&g_session_active, 0, 0) == 1;
        if (active && !registered) {
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
                registered = TRUE;
            } else {
                shim_log("no Chroma server on %s:%d -- retrying", g_host, g_port);
            }
        } else if (!active && registered) {
            if (g_session[0] != '\0') {
                http_request("DELETE", g_session, NULL, NULL, 0);
            }
            g_session[0] = '\0';
            registered = FALSE;
            shim_log("session closed");
        }
        if (!active) {
            continue;  // Nothing to send between UnInit and the next Init.
        }

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

        // Report what the game has been asking for. Done here rather than in
        // the exported functions so no game thread ever touches the log.
        if (g_verbose || now - last_report >= 5000) {
            report_progress();
            last_report = now;
        }
    }

    if (registered && g_session[0] != '\0') {
        http_request("DELETE", g_session, NULL, NULL, 0);
    }
    WSACleanup();
    return 0;
}

// Starts the worker on the first Init and marks the session live. Later Inits
// only flip the flag -- the thread is created once for the life of the process.
static void begin_session(void) {
    if (InterlockedCompareExchange(&g_running, 1, 0) == 0) {
        // Create the event before the thread, so a frame arriving immediately
        // has something to signal.
        g_frame_event = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_worker = CreateThread(NULL, 0, worker_main, NULL, 0, NULL);
        if (g_worker == NULL) {
            InterlockedExchange(&g_running, 0);
            shim_log("could not start the worker thread");
            return;
        }
    }
    InterlockedExchange(&g_session_active, 1);
}

// Asks the worker to finish without waiting for it. Safe from anywhere.
static void signal_worker_stop(void) {
    InterlockedCompareExchange(&g_running, 0, 1);
    if (g_frame_event != NULL) {
        SetEvent(g_frame_event);
    }
}

// Closes the session without touching the worker, which lives as long as the
// process does. This is what UnInit calls, and it must not block: a game calls
// UnInit from its own thread at moments that matter -- Dead Cells does it when
// you die and the run reloads -- and joining a thread there stalls the game for
// as long as the worker takes to notice, which is a visible freeze.
static void end_session(void) {
    InterlockedExchange(&g_session_active, 0);
    if (g_frame_event != NULL) {
        SetEvent(g_frame_event);  // Let the worker post the DELETE promptly.
    }
}

// Replaces whatever the worker had not sent yet. Dropping the previous frame is
// deliberate -- see the note at the top of the file.
static void submit_frame(COLORREF grid[KB_ROWS][KB_COLS]) {
    if (InterlockedCompareExchange(&g_session_active, 0, 0) != 1) {
        return;  // Between UnInit and the next Init.
    }
    EnterCriticalSection(&g_lock);
    memcpy(g_frame, grid, sizeof(g_frame));
    g_frame_pending = TRUE;
    LeaveCriticalSection(&g_lock);

    if (g_frame_event != NULL) {
        SetEvent(g_frame_event);
    }

    InterlockedIncrement(&g_frames_in);  // The worker reports; see g_calls.
}

// --------------------------------------------------------- created effects
//
// The SDK has two ways to show an effect. Create*Effect with a NULL id shows
// it now. With an id it only builds it, and the game shows it later with
// SetEffect -- which is how games pre-build their ambient lighting and flash
// animations at load and switch between them. So an effect created with an id
// is stored here, not shown, until SetEffect names it.
//
// A ring of slots, each id carrying its slot and a generation: no hashing, and
// an id whose slot has since been reused or deleted simply stops matching.
// Games that build animations create and delete effects constantly; 4096
// outstanding ones is far more than any of them keeps.

#define STORE_SLOTS 4096
#define ID_NOT_KEYBOARD 0xFFFFFFFFu

typedef struct {
    DWORD generation;  // 0 = free
    COLORREF grid[KB_ROWS][KB_COLS];
} STORED_EFFECT;

static CRITICAL_SECTION g_store_lock;
static STORED_EFFECT g_store[STORE_SLOTS];
static DWORD g_store_next = 0;
static DWORD g_store_generation = 0;

static volatile LONG g_shown_now = 0;      // Created with a NULL id.
static volatile LONG g_created_later = 0;  // Created with an id, held.
static volatile LONG g_shown_by_set = 0;   // SetEffect on a held keyboard effect.
static volatile LONG g_set_unknown = 0;    // SetEffect on an id we do not hold.

// Ids say "sino" in Data4 so a stray GUID is not mistaken for one of ours.
static void encode_id(RZEFFECTID* id, DWORD slot, DWORD generation) {
    memset(id, 0, sizeof(*id));
    id->Data1 = slot;
    id->Data2 = (WORD)(generation & 0xFFFF);
    id->Data3 = (WORD)(generation >> 16);
    id->Data4[0] = 's';
    id->Data4[1] = 'i';
    id->Data4[2] = 'n';
    id->Data4[3] = 'o';
}

static BOOL decode_id(const RZEFFECTID* id, DWORD* slot, DWORD* generation) {
    if (id->Data4[0] != 's' || id->Data4[1] != 'i' || id->Data4[2] != 'n' || id->Data4[3] != 'o') {
        return FALSE;
    }
    *slot = id->Data1;
    *generation = (DWORD)id->Data2 | ((DWORD)id->Data3 << 16);
    return TRUE;
}

// Holds a keyboard frame for a later SetEffect and hands back its id.
static void store_effect(COLORREF grid[KB_ROWS][KB_COLS], RZEFFECTID* id) {
    EnterCriticalSection(&g_store_lock);
    const DWORD slot = g_store_next++ % STORE_SLOTS;
    if (++g_store_generation == 0) {
        g_store_generation = 1;  // 0 marks a free slot.
    }
    g_store[slot].generation = g_store_generation;
    memcpy(g_store[slot].grid, grid, sizeof(g_store[slot].grid));
    encode_id(id, slot, g_store_generation);
    LeaveCriticalSection(&g_store_lock);
    InterlockedIncrement(&g_created_later);
}

// An id for something with nothing to show on a keyboard -- another device, or
// an animated effect type -- so a later SetEffect on it is a quiet no-op.
static void non_keyboard_id(RZEFFECTID* id) {
    encode_id(id, ID_NOT_KEYBOARD, 0);
}

// Shows a frame now, or holds it for SetEffect when the game asked for an id.
static void show_or_hold(COLORREF grid[KB_ROWS][KB_COLS], RZEFFECTID* effect_id) {
    if (effect_id != NULL) {
        store_effect(grid, effect_id);
        return;
    }
    InterlockedIncrement(&g_shown_now);
    submit_frame(grid);
}

// Razer keyboards, as CreateEffect names them. The generic CreateEffect works
// for any device, and a mouse or headset grid is far smaller than a keyboard's
// 6x22 -- reading one as a keyboard frame both runs off the end of the game's
// buffer and paints garbage on the keys. So only these are forwarded; any
// other GUID is logged, and a keyboard missing here can be added from that.
static const GUID kKeyboardGuids[] = {
    {0x2EA1BB63, 0xCA28, 0x428D, {0x9F, 0x06, 0x19, 0x6B, 0x88, 0x33, 0x0B, 0xBB}},  // BlackWidow Chroma
    {0xED1C1B82, 0xBFBE, 0x418F, {0xB4, 0x9D, 0xD0, 0x3F, 0x05, 0xB1, 0x49, 0xDF}},  // BlackWidow Chroma TE
    {0x18C5AD9B, 0x4326, 0x4828, {0x92, 0xC4, 0x26, 0x69, 0xA6, 0x6D, 0x22, 0x83}},  // DeathStalker Chroma
    {0x872AB2A9, 0x7959, 0x4478, {0x9F, 0xED, 0x15, 0xF6, 0x18, 0x6E, 0x72, 0xE4}},  // Overwatch keyboard
    {0x5AF60076, 0xADE9, 0x43D4, {0xB5, 0x74, 0x52, 0x59, 0x92, 0x93, 0xB5, 0x54}},  // BlackWidow X Chroma
    {0x2D84DD51, 0x3290, 0x4AAC, {0x9A, 0x89, 0xD8, 0xAF, 0xDE, 0x38, 0xB5, 0x7C}},  // BlackWidow X TE Chroma
    {0x803378C1, 0xCC48, 0x4970, {0x85, 0x39, 0xD8, 0x28, 0xCC, 0x1D, 0x42, 0x0A}},  // Ornata Chroma
    {0xC83BDFE8, 0xE7FC, 0x40E0, {0x99, 0xDB, 0x87, 0x2E, 0x23, 0xF1, 0x98, 0x91}},  // Blade Stealth
    {0xF2BEDFAF, 0xA0FE, 0x4651, {0x9D, 0x41, 0xB6, 0xCE, 0x60, 0x3A, 0x3D, 0xDD}},  // Blade
    {0xA73AC338, 0xF0E5, 0x4BF7, {0x91, 0xAE, 0xDD, 0x1F, 0x7E, 0x17, 0x37, 0xA5}},  // Blade Pro
};

#define SEEN_GUIDS 16
static GUID g_seen_guids[SEEN_GUIDS];
static volatile LONG g_seen_count = 0;

static BOOL is_keyboard_guid(const GUID* id) {
    for (size_t i = 0; i < sizeof(kKeyboardGuids) / sizeof(kKeyboardGuids[0]); ++i) {
        if (memcmp(id, &kKeyboardGuids[i], sizeof(GUID)) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

// Remembers a device GUID a game used, for the worker to log once.
static void note_device(const GUID* id) {
    EnterCriticalSection(&g_store_lock);
    const LONG count = g_seen_count;
    BOOL known = FALSE;
    for (LONG i = 0; i < count; ++i) {
        if (memcmp(&g_seen_guids[i], id, sizeof(GUID)) == 0) {
            known = TRUE;
            break;
        }
    }
    if (!known && count < SEEN_GUIDS) {
        g_seen_guids[count] = *id;
        InterlockedExchange(&g_seen_count, count + 1);
    }
    LeaveCriticalSection(&g_store_lock);
}

// Turns the counters the exported functions bump into log lines. Called only
// from the worker, so the log is never written from a game's thread.
static void report_progress(void) {
    static LONG reported_frames = 0;
    static LONG reported_unhandled = -1;
    static LONG reported_calls[CALL_KIND_COUNT] = {0};
    static LONG reported_modes = 0;
    static LONG reported_guids = 0;

    const LONG in = InterlockedCompareExchange(&g_frames_in, 0, 0);
    const LONG sent = InterlockedCompareExchange(&g_frames_sent, 0, 0);
    if (in != reported_frames) {
        shim_log("%ld frames from the game, %ld forwarded", in, sent);
        reported_frames = in;
    }
    const LONG unhandled = InterlockedCompareExchange(&g_unhandled_effect, -1, -1);
    if (unhandled != reported_unhandled) {
        shim_log("keyboard effect type %ld carries no frame; ignoring it", unhandled);
        reported_unhandled = unhandled;
    }
    // Which of the SDK's two modes the game uses, and whether its
    // SetEffect calls find what they name.
    const LONG now_count = InterlockedCompareExchange(&g_shown_now, 0, 0);
    const LONG later = InterlockedCompareExchange(&g_created_later, 0, 0);
    const LONG by_set = InterlockedCompareExchange(&g_shown_by_set, 0, 0);
    const LONG unknown = InterlockedCompareExchange(&g_set_unknown, 0, 0);
    if (now_count + later + by_set + unknown != reported_modes) {
        shim_log("keyboard: %ld shown at once, %ld created for later, %ld shown by SetEffect,"
                 " %ld SetEffect on unknown ids",
                 now_count, later, by_set, unknown);
        reported_modes = now_count + later + by_set + unknown;
    }
    const LONG seen = InterlockedCompareExchange(&g_seen_count, 0, 0);
    for (LONG i = reported_guids; i < seen; ++i) {
        const GUID* g = &g_seen_guids[i];
        shim_log("CreateEffect for device {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}%s",
                 (unsigned long)g->Data1, g->Data2, g->Data3, g->Data4[0], g->Data4[1],
                 g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7],
                 is_keyboard_guid(g) ? " (a keyboard: forwarded)" : " (not a known keyboard: ignored)");
    }
    reported_guids = seen;
    for (int kind = 0; kind < CALL_KIND_COUNT; ++kind) {
        const LONG calls = InterlockedCompareExchange(&g_calls[kind], 0, 0);
        if (calls != reported_calls[kind]) {
            shim_log("%s: %ld calls%s", kCallNames[kind], calls,
                     kind == CALL_GENERIC ? "" : " (not forwarded)");
            reported_calls[kind] = calls;
        }
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
    begin_session();
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT InitSDK(void* app_info) {
    (void)app_info;
    shim_log("InitSDK");
    begin_session();
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT UnInit(void) {
    shim_log("UnInit (%ld frames in, %ld forwarded)",
             InterlockedCompareExchange(&g_frames_in, 0, 0),
             InterlockedCompareExchange(&g_frames_sent, 0, 0));
    end_session();  // Returns at once; see the note on end_session.
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateKeyboardEffect(int effect, void* param, RZEFFECTID* effect_id) {
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
            // animations with no frame to forward. Recorded so it is obvious
            // when a game only ever asks for these.
            InterlockedExchange(&g_unhandled_effect, (LONG)effect);
            if (effect_id != NULL) {
                non_keyboard_id(effect_id);
            }
            return RZRESULT_SUCCESS;
        }
    }

    show_or_hold(grid, effect_id);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateEffect(RZDEVICEID device_id, int effect, void* param,
                                            RZEFFECTID* effect_id) {
    InterlockedIncrement(&g_calls[CALL_GENERIC]);
    note_device(&device_id);

    // Only a keyboard's payload is a 6x22 grid; see kKeyboardGuids.
    if (is_keyboard_guid(&device_id) && param != NULL &&
        (effect == GENERIC_EFFECT_CUSTOM || effect == GENERIC_EFFECT_STATIC)) {
        COLORREF grid[KB_ROWS][KB_COLS];
        if (effect == GENERIC_EFFECT_CUSTOM) {
            memcpy(grid, param, sizeof(grid));
        } else {
            fill_uniform(grid, ((KB_STATIC_EFFECT*)param)->Color);
        }
        show_or_hold(grid, effect_id);
        return RZRESULT_SUCCESS;
    }
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateMouseEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    (void)effect;
    InterlockedIncrement(&g_calls[CALL_MOUSE]);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateHeadsetEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    (void)effect;
    InterlockedIncrement(&g_calls[CALL_HEADSET]);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateMousepadEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    (void)effect;
    InterlockedIncrement(&g_calls[CALL_MOUSEPAD]);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateKeypadEffect(int effect, void* param, RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    (void)effect;
    InterlockedIncrement(&g_calls[CALL_KEYPAD]);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT CreateChromaLinkEffect(int effect, void* param,
                                                      RZEFFECTID* effect_id) {
    (void)param;
    if (effect_id != NULL) {
        non_keyboard_id(effect_id);
    }
    (void)effect;
    InterlockedIncrement(&g_calls[CALL_CHROMALINK]);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT SetEffect(RZEFFECTID effect_id) {
    DWORD slot = 0;
    DWORD generation = 0;
    if (!decode_id(&effect_id, &slot, &generation) || slot == ID_NOT_KEYBOARD) {
        // Another device's effect, or nothing we issued: nothing to show.
        if (slot != ID_NOT_KEYBOARD) {
            InterlockedIncrement(&g_set_unknown);
        }
        return RZRESULT_SUCCESS;
    }

    COLORREF grid[KB_ROWS][KB_COLS];
    BOOL found = FALSE;
    EnterCriticalSection(&g_store_lock);
    if (slot < STORE_SLOTS && generation != 0 && g_store[slot].generation == generation) {
        memcpy(grid, g_store[slot].grid, sizeof(grid));
        found = TRUE;
    }
    LeaveCriticalSection(&g_store_lock);

    if (!found) {
        // Deleted, or evicted from the ring. Success anyway: an error here
        // could make a game give up on lighting for the whole session.
        InterlockedIncrement(&g_set_unknown);
        return RZRESULT_SUCCESS;
    }
    InterlockedIncrement(&g_shown_by_set);
    submit_frame(grid);
    return RZRESULT_SUCCESS;
}

__declspec(dllexport) RZRESULT DeleteEffect(RZEFFECTID effect_id) {
    DWORD slot = 0;
    DWORD generation = 0;
    if (decode_id(&effect_id, &slot, &generation) && slot < STORE_SLOTS) {
        EnterCriticalSection(&g_store_lock);
        if (g_store[slot].generation == generation) {
            g_store[slot].generation = 0;
        }
        LeaveCriticalSection(&g_store_lock);
    }
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
            InitializeCriticalSection(&g_store_lock);
            load_config();
            shim_log("loaded, forwarding to %s:%d", g_host, g_port);
            // No thread or socket work here: DllMain runs under the loader
            // lock, and a worker started from it cannot run until we return.
            break;
        case DLL_PROCESS_DETACH:
            // Signal only. Joining here would deadlock (see stop_worker), and
            // the critical section is left alone because the worker may still
            // be inside it -- a few leaked bytes on unload beats a hang, and on
            // process exit none of it matters anyway.
            InterlockedExchange(&g_session_active, 0);
            signal_worker_stop();
            break;
        default:
            break;
    }
    return TRUE;
}
