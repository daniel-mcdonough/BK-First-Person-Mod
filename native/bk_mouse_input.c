/*
 * bk_mouse_input.c — X11 mouse capture for BK first-person mode
 *
 * Native Linux shared library loaded by BK:Recompiled at runtime.
 * Uses warp-to-center to compute mouse deltas each frame.
 *
 * All exported functions use the Recomp calling convention:
 *   void func(uint8_t* rdram, recomp_context* ctx)
 * Arguments are read from ctx->r4 (a0), ctx->r5 (a1), etc.
 * Integer returns are written to ctx->r2 (v0).
 *
 * Build:
 *   gcc -shared -fPIC -Wall -Wextra -o build/bk_mouse_input.so \
 *       native/bk_mouse_input.c -lX11 -lXfixes
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
/* X11 headers — only on Linux                                         */
/* ------------------------------------------------------------------ */

#ifdef __linux__
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
  #include <X11/extensions/Xfixes.h>
#endif

/* ------------------------------------------------------------------ */
/* Recomp native API version (required by the mod loader)              */
/* ------------------------------------------------------------------ */

EXPORT uint32_t recomp_api_version = 1;

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

#ifdef __linux__
static Display *dpy;              /* X11 connection (opened once)       */
static int      delta_x, delta_y; /* last-frame mouse deltas            */
static int      fp_wants_mouse;   /* MIPS sets this on FP enter/exit    */
static int      user_paused;      /* toggled by "2" key                 */
static int      captured;         /* currently capturing? (composite)   */
static int      cursor_hidden;    /* is cursor hidden via XFixes?       */
#endif

/* ------------------------------------------------------------------ */
/* Lifecycle: open/close X11 display                                   */
/* ------------------------------------------------------------------ */

#ifdef __linux__

__attribute__((constructor))
static void mouse_init(void) {
    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return; /* pure Wayland without XWayland — graceful no-op */

    delta_x = 0;
    delta_y = 0;
    fp_wants_mouse = 0;
    user_paused = 0;
    captured = 0;
    cursor_hidden = 0;
}

__attribute__((destructor))
static void mouse_shutdown(void) {
    if (!dpy)
        return;

    if (cursor_hidden)
        XFixesShowCursor(dpy, DefaultRootWindow(dpy));

    XCloseDisplay(dpy);
    dpy = NULL;
}

/* ------------------------------------------------------------------ */
/* Internal: cursor show/hide via XFixes (server-level, not overridable) */
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

/* ------------------------------------------------------------------ */
/* Key constants for "2" key toggle (X11 keycode)                      */
/* ------------------------------------------------------------------ */
#define KEY_2_KEYCODE  11   /* XK_2 on most keymaps; row-0 digit "2"   */

static int key2_was_down;

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

    delta_x = 0;
    delta_y = 0;

    if (!dpy)
        return;

    /* Check "2" key toggle (rising edge) */
    {
        char keys[32];
        int byte_idx = KEY_2_KEYCODE / 8;
        int bit_idx  = KEY_2_KEYCODE % 8;
        int down;

        XQueryKeymap(dpy, keys);
        down = (keys[byte_idx] >> bit_idx) & 1;

        if (down && !key2_was_down)
            user_paused = !user_paused;
        key2_was_down = down;
    }

    /* Composite capture decision */
    should_capture = fp_wants_mouse && !user_paused;

    /* Get focused window */
    XGetInputFocus(dpy, &focus_win, &revert);
    if (focus_win == None || focus_win == PointerRoot) {
        captured = 0;
        show_cursor();
        return;
    }

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
        delta_x = 0;
        delta_y = 0;
        if (dpy)
            show_cursor();
    }
}

#endif /* __linux__ */

/* ------------------------------------------------------------------ */
/* Exported API — Recomp calling convention                            */
/*   void func(uint8_t* rdram, recomp_context* ctx)                    */
/*   args: ctx->r4 (a0), ctx->r5 (a1), ...                            */
/*   int return: ctx->r2 (v0)                                         */
/* ------------------------------------------------------------------ */

EXPORT void mouse_poll(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
#ifdef __linux__
    do_mouse_poll();
#endif
}

EXPORT void mouse_get_delta_x(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#ifdef __linux__
    ctx->r2 = (int32_t)delta_x;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_get_delta_y(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#ifdef __linux__
    ctx->r2 = (int32_t)delta_y;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_set_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#ifdef __linux__
    do_mouse_set_enabled((int)ctx->r4);
#else
    (void)ctx;
#endif
}

EXPORT void mouse_is_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#ifdef __linux__
    ctx->r2 = (int32_t)fp_wants_mouse;
#else
    ctx->r2 = 0;
#endif
}

EXPORT void mouse_is_captured(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
#ifdef __linux__
    ctx->r2 = (int32_t)captured;
#else
    ctx->r2 = 0;
#endif
}
