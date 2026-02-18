/*
 * bk_mouse_input.c — Mouse capture for BK first-person mode
 *
 * Native shared library loaded by BK:Recompiled at runtime.
 * Uses warp-to-center to compute mouse deltas each frame.
 *
 * All exported functions use the Recomp calling convention:
 *   void func(uint8_t* rdram, recomp_context* ctx)
 * Arguments are read from ctx->r4 (a0), ctx->r5 (a1), etc.
 * Integer returns are written to ctx->r2 (v0).
 *
 * Build (Linux):
 *   gcc -shared -fPIC -Wall -Wextra -o build/bk_mouse_input.so \
 *       native/bk_mouse_input.c -lX11 -lXfixes
 *
 * Build (Windows cross-compile):
 *   x86_64-w64-mingw32-gcc -shared -Wall -Wextra -o build/bk_mouse_input.dll \
 *       native/bk_mouse_input.c
 */

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Platform export macro                                               */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
  #define EXPORT __declspec(dllexport)
#else
  #define EXPORT __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------ */
/* Minimal Recomp types (must match recomp.h layout)                   */
/* ------------------------------------------------------------------ */

typedef uint64_t gpr;

typedef union {
    double d;
    struct { float fl; float fh; };
    struct { uint32_t u32l; uint32_t u32h; };
    uint64_t u64;
} fpr;

typedef struct {
    gpr r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
        r8,  r9,  r10, r11, r12, r13, r14, r15,
        r16, r17, r18, r19, r20, r21, r22, r23,
        r24, r25, r26, r27, r28, r29, r30, r31;
    fpr f0,  f1,  f2,  f3,  f4,  f5,  f6,  f7,
        f8,  f9,  f10, f11, f12, f13, f14, f15,
        f16, f17, f18, f19, f20, f21, f22, f23,
        f24, f25, f26, f27, f28, f29, f30, f31;
    uint64_t hi, lo;
    uint32_t* f_odd;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
} recomp_context;

/* ------------------------------------------------------------------ */
/* Platform headers                                                    */
/* ------------------------------------------------------------------ */

#ifdef __linux__
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
  #include <X11/extensions/Xfixes.h>
  #include <X11/cursorfont.h>
  #include <time.h>
  #include <pthread.h>
  #include <unistd.h>
#elif defined(_WIN32)
  #include <windows.h>
#endif

/* ------------------------------------------------------------------ */
/* Recomp native API version (required by the mod loader)              */
/* ------------------------------------------------------------------ */

EXPORT uint32_t recomp_api_version = 1;

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

#if defined(__linux__)
static Display *dpy;              /* X11 connection (opened once)       */
static Cursor   arrow_cursor;    /* standard arrow cursor for restoring */
static Window   cached_focus_win; /* last known focused window          */
static int      delta_x, delta_y; /* last-frame mouse deltas            */
static int      fp_wants_mouse;   /* MIPS sets this on FP enter/exit    */
static int      user_paused;      /* toggled by "2" key                 */
static int      esc_paused;       /* toggled by Escape key (menu open)  */
static int      captured;         /* currently capturing? (composite)   */
static int      cursor_hidden;    /* is cursor hidden via XFixes?       */
static uint64_t last_poll_ms;     /* timestamp of last poll (ms)        */
static pthread_t watchdog_thread;
static volatile int watchdog_running;
#elif defined(_WIN32)
static int      delta_x, delta_y; /* last-frame mouse deltas            */
static int      fp_wants_mouse;   /* MIPS sets this on FP enter/exit    */
static int      user_paused;      /* toggled by "2" key                 */
static int      esc_paused;       /* toggled by Escape key (menu open)  */
static int      captured;         /* currently capturing? (composite)   */
static int      cursor_hidden;    /* have we called ShowCursor(FALSE)?  */
static uint64_t last_poll_ms;     /* timestamp of last poll (ms)        */
#endif

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

#ifdef __linux__

/* Watchdog interval and threshold (ms) */
#define WATCHDOG_INTERVAL_MS  100
#define WATCHDOG_THRESHOLD_MS 200

/* Watchdog: show cursor if mouse_poll hasn't been called recently.
 * Runs on a background thread; requires XInitThreads(). */
static void *watchdog_func(void *arg) {
    (void)arg;
    while (watchdog_running) {
        usleep(WATCHDOG_INTERVAL_MS * 1000);
        if (!dpy || !cursor_hidden || !fp_wants_mouse)
            continue;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
        if (last_poll_ms != 0 && (now - last_poll_ms) > WATCHDOG_THRESHOLD_MS) {
            XFixesShowCursor(dpy, DefaultRootWindow(dpy));
            if (cached_focus_win && arrow_cursor)
                XDefineCursor(dpy, cached_focus_win, arrow_cursor);
            XFlush(dpy);
            cursor_hidden = 0;
        }
    }
    return NULL;
}

__attribute__((constructor))
static void mouse_init(void) {
    XInitThreads();

    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return; /* pure Wayland without XWayland — graceful no-op */

    arrow_cursor = XCreateFontCursor(dpy, XC_left_ptr);
    cached_focus_win = None;
    delta_x = 0;
    delta_y = 0;
    fp_wants_mouse = 0;
    user_paused = 0;
    esc_paused = 0;
    captured = 0;
    cursor_hidden = 0;
    last_poll_ms = 0;

    watchdog_running = 1;
    pthread_create(&watchdog_thread, NULL, watchdog_func, NULL);
}

__attribute__((destructor))
static void mouse_shutdown(void) {
    watchdog_running = 0;
    if (watchdog_thread) {
        pthread_join(watchdog_thread, NULL);
        watchdog_thread = 0;
    }

    if (!dpy)
        return;

    if (cursor_hidden)
        XFixesShowCursor(dpy, DefaultRootWindow(dpy));

    if (arrow_cursor)
        XFreeCursor(dpy, arrow_cursor);

    XCloseDisplay(dpy);
    dpy = NULL;
}

/* ------------------------------------------------------------------ */
/* Internal: cursor show/hide                                          */
/* ------------------------------------------------------------------ */

static void hide_cursor(void) {
    if (!cursor_hidden) {
        XFixesHideCursor(dpy, DefaultRootWindow(dpy));
        XFlush(dpy);
        cursor_hidden = 1;
    }
}

static void show_cursor(void) {
    if (cursor_hidden) {
        XFixesShowCursor(dpy, DefaultRootWindow(dpy));
        XFlush(dpy);
        cursor_hidden = 0;
    }
}

/* Force-show cursor with a real arrow image (overrides SDL's blank cursor).
 * Called from MIPS hooks every frame during pause menus. */
static void do_mouse_force_show_cursor(void) {
    if (!dpy)
        return;
    if (cursor_hidden) {
        XFixesShowCursor(dpy, DefaultRootWindow(dpy));
        cursor_hidden = 0;
    }
    if (cached_focus_win && arrow_cursor)
        XDefineCursor(dpy, cached_focus_win, arrow_cursor);
    XFlush(dpy);
}

/* ------------------------------------------------------------------ */
/* Key constants (X11 keycodes)                                        */
/* ------------------------------------------------------------------ */
#define KEY_2_KEYCODE    11   /* XK_2 on most keymaps; row-0 digit "2"   */
#define KEY_ESC_KEYCODE   9   /* Escape key X11 keycode                  */

/* Time gap threshold (ms) — if poll gap exceeds this, game was paused */
#define ESC_GAP_THRESHOLD_MS 200

static int key2_was_down;
static int esc_was_down;

static uint64_t get_time_ms_linux(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ------------------------------------------------------------------ */
/* Internal poll logic (called by the exported wrapper)                 */
/* ------------------------------------------------------------------ */

static void do_mouse_poll(void) {
    Window focus_win;
    int    revert;
    int    root_x, root_y, win_x, win_y;
    unsigned int mask;
    Window root_ret, child_ret;
    XWindowAttributes attr;
    int cx, cy;
    int should_capture;
    uint64_t now;

    delta_x = 0;
    delta_y = 0;

    if (!dpy)
        return;

    /* Auto-clear esc_paused after a time gap (game was paused then resumed) */
    now = get_time_ms_linux();
    if (esc_paused && last_poll_ms != 0 && (now - last_poll_ms) > ESC_GAP_THRESHOLD_MS)
        esc_paused = 0;
    last_poll_ms = now;

    /* Check key toggles (rising edge) */
    {
        char keys[32];
        int byte_idx = KEY_2_KEYCODE / 8;
        int bit_idx  = KEY_2_KEYCODE % 8;
        int down;
        int esc_byte = KEY_ESC_KEYCODE / 8;
        int esc_bit  = KEY_ESC_KEYCODE % 8;
        int esc_down;

        XQueryKeymap(dpy, keys);

        /* "2" key toggle */
        down = (keys[byte_idx] >> bit_idx) & 1;
        if (down && !key2_was_down)
            user_paused = !user_paused;
        key2_was_down = down;

        /* Escape key toggle (for Recomp menu) */
        esc_down = (keys[esc_byte] >> esc_bit) & 1;
        if (esc_down && !esc_was_down)
            esc_paused = !esc_paused;
        esc_was_down = esc_down;
    }

    /* Composite capture decision */
    should_capture = fp_wants_mouse && !user_paused && !esc_paused;

    /* Get focused window */
    XGetInputFocus(dpy, &focus_win, &revert);
    if (focus_win == None || focus_win == PointerRoot) {
        captured = 0;
        show_cursor();
        return;
    }
    cached_focus_win = focus_win;

    if (!should_capture) {
        captured = 0;
        show_cursor();
        return;
    }

    /* Get window geometry for center computation */
    if (!XGetWindowAttributes(dpy, focus_win, &attr)) {
        captured = 0;
        show_cursor();
        return;
    }

    cx = attr.width  / 2;
    cy = attr.height / 2;

    /* Query current pointer position relative to the focused window */
    if (!XQueryPointer(dpy, focus_win, &root_ret, &child_ret,
                       &root_x, &root_y, &win_x, &win_y, &mask)) {
        captured = 0;
        show_cursor();
        return;
    }

    /* Compute deltas from center */
    delta_x = win_x - cx;
    delta_y = win_y - cy;

    /* Warp pointer back to center */
    XWarpPointer(dpy, None, focus_win, 0, 0, 0, 0, cx, cy);

    /* Hide cursor while captured */
    hide_cursor();

    XFlush(dpy);
    captured = 1;
}

static void do_mouse_set_enabled(int enabled) {
    fp_wants_mouse = enabled;
    if (!enabled) {
        captured = 0;
        user_paused = 0;
        esc_paused = 0;
        delta_x = 0;
        delta_y = 0;
        if (dpy)
            show_cursor();
    }
}

#endif /* __linux__ */

/* ------------------------------------------------------------------ */
/* Win32 implementation                                                */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

/* ------------------------------------------------------------------ */
/* Lifecycle: DllMain                                                  */
/* ------------------------------------------------------------------ */

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL; (void)lpvReserved;
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        delta_x = 0;
        delta_y = 0;
        fp_wants_mouse = 0;
        user_paused = 0;
        esc_paused = 0;
        captured = 0;
        cursor_hidden = 0;
        last_poll_ms = 0;
        break;
    case DLL_PROCESS_DETACH:
        if (cursor_hidden) {
            ShowCursor(TRUE);
            cursor_hidden = 0;
        }
        break;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Internal: cursor show/hide (ShowCursor is reference-counted)        */
/* ------------------------------------------------------------------ */

static void hide_cursor_win32(void) {
    if (!cursor_hidden) {
        ShowCursor(FALSE);
        cursor_hidden = 1;
    }
}

static void show_cursor_win32(void) {
    if (cursor_hidden) {
        ShowCursor(TRUE);
        cursor_hidden = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Key toggle state                                                    */
/* ------------------------------------------------------------------ */

static int key2_was_down;
static int esc_was_down;

/* Time gap threshold (ms) */
#define ESC_GAP_THRESHOLD_MS_WIN 200

/* ------------------------------------------------------------------ */
/* Internal poll logic                                                 */
/* ------------------------------------------------------------------ */

static void do_mouse_poll_win32(void) {
    HWND hwnd;
    RECT rect;
    POINT center, cursor;
    int should_capture;
    int key_down, esc_down;
    uint64_t now;

    delta_x = 0;
    delta_y = 0;

    /* Auto-clear esc_paused after a time gap (game was paused then resumed) */
    now = (uint64_t)GetTickCount64();
    if (esc_paused && last_poll_ms != 0 && (now - last_poll_ms) > ESC_GAP_THRESHOLD_MS_WIN)
        esc_paused = 0;
    last_poll_ms = now;

    /* Check "2" key toggle (rising edge) — VK key code 0x32 */
    key_down = (GetAsyncKeyState(0x32) & 0x8000) != 0;
    if (key_down && !key2_was_down)
        user_paused = !user_paused;
    key2_was_down = key_down;

    /* Escape key toggle (rising edge, for Recomp menu) */
    esc_down = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (esc_down && !esc_was_down)
        esc_paused = !esc_paused;
    esc_was_down = esc_down;

    should_capture = fp_wants_mouse && !user_paused && !esc_paused;

    /* Get the foreground (focused) window */
    hwnd = GetForegroundWindow();
    if (!hwnd) {
        captured = 0;
        show_cursor_win32();
        return;
    }

    if (!should_capture) {
        captured = 0;
        show_cursor_win32();
        return;
    }

    /* Get client area and compute center in screen coords */
    if (!GetClientRect(hwnd, &rect)) {
        captured = 0;
        show_cursor_win32();
        return;
    }

    center.x = (rect.right - rect.left) / 2;
    center.y = (rect.bottom - rect.top) / 2;
    ClientToScreen(hwnd, &center);

    /* Get current cursor position (screen coords) */
    if (!GetCursorPos(&cursor)) {
        captured = 0;
        show_cursor_win32();
        return;
    }

    /* Compute deltas from center */
    delta_x = cursor.x - center.x;
    delta_y = cursor.y - center.y;

    /* Warp cursor back to center */
    SetCursorPos(center.x, center.y);

    /* Hide cursor while captured */
    hide_cursor_win32();

    captured = 1;
}

static void do_mouse_set_enabled_win32(int enabled) {
    fp_wants_mouse = enabled;
    if (!enabled) {
        captured = 0;
        user_paused = 0;
        esc_paused = 0;
        delta_x = 0;
        delta_y = 0;
        show_cursor_win32();
    }
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* Exported API — Recomp calling convention                            */
/*   void func(uint8_t* rdram, recomp_context* ctx)                    */
/*   args: ctx->r4 (a0), ctx->r5 (a1), ...                            */
/*   int return: ctx->r2 (v0)                                         */
/* ------------------------------------------------------------------ */

EXPORT void mouse_poll(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
#if defined(__linux__)
    do_mouse_poll();
#elif defined(_WIN32)
    do_mouse_poll_win32();
#endif
}

EXPORT void mouse_get_delta_x(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#if defined(__linux__) || defined(_WIN32)
    ctx->r2 = (int32_t)delta_x;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_get_delta_y(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#if defined(__linux__) || defined(_WIN32)
    ctx->r2 = (int32_t)delta_y;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_set_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#if defined(__linux__)
    do_mouse_set_enabled((int)ctx->r4);
#elif defined(_WIN32)
    do_mouse_set_enabled_win32((int)ctx->r4);
#else
    (void)ctx;
#endif
}

EXPORT void mouse_is_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#if defined(__linux__) || defined(_WIN32)
    ctx->r2 = (int32_t)fp_wants_mouse;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_is_captured(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#if defined(__linux__) || defined(_WIN32)
    ctx->r2 = (int32_t)captured;
#else
    ctx->r2 = 0;
#endif
}

/* Force-show the cursor with a visible arrow image.
 * Called from MIPS pause menu hooks every frame to override SDL's blank cursor. */
EXPORT void mouse_force_show_cursor(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
#if defined(__linux__)
    do_mouse_force_show_cursor();
#elif defined(_WIN32)
    show_cursor_win32();
#endif
}
