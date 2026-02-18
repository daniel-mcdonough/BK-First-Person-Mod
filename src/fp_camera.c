#include "modding.h"
#include "recomputils.h"
#include "recompconfig.h"
#include "PR/ultratypes.h"

/* ------------------------------------------------------------------ */
/* Base game function declarations (resolved via syms.toml)            */
/* ------------------------------------------------------------------ */

int  bakey_pressed(s32 button);
u32  bakey_held(s32 button);
int  can_view_first_person(void);
void player_getPosition(f32 dst[3]);
void player_setModelVisible(s32 visible);
u32  player_getTransformation(void);
f32  player_getYaw(void);
void yaw_set(f32 yaw);
void yaw_setIdeal(f32 yaw);
s32  player_getWaterState(void);
s32  player_isDead(void);
s32  bs_getState(void);
s32  map_get(void);
f32  time_getDelta(void);
f32  mlNormalizeAngle(f32 angle);
void viewport_setPosition_vec3f(f32 src[3]);
void viewport_setRotation_vec3f(f32 src[3]);
void viewport_getRotation_vec3f(f32 dst[3]);
void baModel_802924E8(f32 dst[3]);
void baModel_80291A50(s32 bone_index, f32 dst[3]);
f32  gu_sqrtf(f32 x);
f32  ml_sin_deg(f32 deg);
f32  ml_cos_deg(f32 deg);
f32  viewport_getFOVy(void);
void viewport_setFOVy(f32 fovy);

/* ------------------------------------------------------------------ */
/* Native mouse input library (imported from own mod's native .so)     */
/* ------------------------------------------------------------------ */

RECOMP_IMPORT(".", void mouse_poll(void));
RECOMP_IMPORT(".", int  mouse_get_delta_x(void));
RECOMP_IMPORT(".", int  mouse_get_delta_y(void));
RECOMP_IMPORT(".", void mouse_set_enabled(int enabled));
RECOMP_IMPORT(".", int  mouse_is_enabled(void));
RECOMP_IMPORT(".", int  mouse_is_captured(void));

/* Player model rotation (degrees, used by renderer — captures full rolls/flips) */
f32  pitch_get(void);
s32  bastick_getZone(void);

/* ------------------------------------------------------------------ */
/* Button constants (from enums.h)                                    */
/* ------------------------------------------------------------------ */

#define BUTTON_D_UP    0x4
#define BUTTON_C_LEFT  0xA
#define BUTTON_C_DOWN  0xB
#define BUTTON_C_UP    0xC
#define BUTTON_C_RIGHT 0xD

#define BS_EGG_HEAD    0x9
#define BS_EGG_ASS     0xA

#define BONE_LEFT_ARM  8
#define BONE_RIGHT_ARM 7
#define BONE_HEAD      9
#define BONE_BODY      5

#define BS_BEE_FLY     0x8C

#define BS_BTROT_JUMP  0x8
#define BS_BTROT_IDLE  0x15
#define BS_BTROT_WALK  0x16
#define BS_BTROT_EXIT  0x17
#define BS_BTROT_SLIDE 0x45

#define BS_FLY         0x24
#define BS_BOMB        0x2A

#define BS_LONGLEG_IDLE  0x26
#define BS_LONGLEG_WALK  0x27
#define BS_LONGLEG_JUMP  0x28
#define BS_LONGLEG_SLIDE 0x55

/* ------------------------------------------------------------------ */
/* Tuning constants                                                    */
/* ------------------------------------------------------------------ */

#define FP_LOOK_SPEED    120.0f   /* degrees per second                */
#define FP_PITCH_MIN    (-80.0f)  /* look-up limit                     */
#define FP_PITCH_MAX      80.0f   /* look-down limit                   */
#define FP_EYE_Y_BOOST   30.0f   /* extra height to reach eye level   */

/* Per-transformation eye heights (units above player position)       */
#define TRANSFORM_BANJO    1
#define TRANSFORM_TERMITE  2
#define TRANSFORM_PUMPKIN  3
#define TRANSFORM_WALRUS   4
#define TRANSFORM_CROC     5
#define TRANSFORM_BEE      6
#define TRANSFORM_WASHUP   7
#define FP_BOB_SMOOTH     6.0f   /* Y-smoothing speed (higher = less damping) */
#define FP_GEO_PITCH_MAX   5.0f  /* max geometric pitch in degrees (limits run/jump lean) */
#define FP_GEO_ROLL_MAX   10.0f  /* max geometric roll in degrees (limits walk tilt)      */

/* Synthetic head bob for transformations without bone data */
#define FP_SYNTH_BOB_PUMPKIN_IDLE  310.0f   /* deg/sec  (idle hop, slightly faster)            */
#define FP_SYNTH_BOB_PUMPKIN_WALK 2466.0f   /* deg/sec  (50 cycles / 7.3 sec × 360)           */
#define FP_SYNTH_BOB_PUMPKIN_AMP     1.5f   /* Y units — small for rapid walk cycle            */
#define FP_SYNTH_BOB_PUMPKIN_IDLE_AMP 3.0f  /* Y units — larger for slow idle hop              */
#define FP_BEE_WALK_FREQ          720.0f   /* deg/sec  (10 cycles / 5 sec × 360)              */
#define FP_BEE_WALK_ROLL            3.0f   /* degrees of roll rotation (body dip)             */

/* Termite sway: side-to-side with downward dip at extremes */
#define FP_TERMITE_SWAY_FREQ     300.0f   /* deg/sec  (10 cycles / 12 sec × 360)             */
#define FP_TERMITE_SWAY_HORIZ      3.0f   /* horizontal sway amplitude (perpendicular to yaw) */
#define FP_TERMITE_SWAY_DIP        2.0f   /* vertical dip amplitude at extremes               */

/* Talon trot bob */
#define FP_TROT_BOB_FREQ          360.0f   /* deg/sec  (50 bobs / 50 sec × 360)              */
#define FP_TROT_BOB_AMP             2.0f   /* Y units                                         */

/* Washing machine sway amplitudes */
#define FP_WASHUP_WALK_FREQ      720.0f   /* deg/sec  (20 cycles / 10 sec × 360)             */
#define FP_WASHUP_SWAY_HORIZ     10.0f   /* horizontal sway amplitude (walk + idle)          */
#define FP_WASHUP_SWAY_VERT       7.0f   /* vertical arc amplitude (walk)                    */
#define FP_WASHUP_IDLE_SWAY        5.0f   /* idle harmonic sway amplitude                     */

/* Bee idle sway: asymmetric harmonic — sin(p) + 0.35*sin(3p) */
#define FP_BEE_IDLE_FREQ         120.0f   /* deg/sec  (10 cycles / 30 sec × 360)             */
#define FP_BEE_IDLE_SWAY           3.0f   /* horizontal sway amplitude                       */
#define FP_BEE_IDLE_HARMONIC       0.35f  /* 3rd harmonic weight (creates double-left bounce) */
#define FP_FLIGHT_ROLL_SCALE       0.25f  /* roll = turn_rate * this (deg roll per deg/sec)    */
#define FP_FLIGHT_ROLL_MAX        30.0f  /* max flight roll in degrees                        */

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static s32 fp_active;
static f32 fp_yaw;
static f32 fp_pitch;
static s32 fp_last_map;
static u32 fp_last_transformation;
static s32 fp_prev_toggle_held;          /* for rising-edge detection    */
static f32 fp_smooth_y;                 /* smoothed eye Y position       */
static f32 fp_smooth_roll;              /* smoothed roll angle           */
static f32 fp_bob_phase;               /* synthetic bob sine phase (degrees) */
static f32 fp_bob_strength;            /* 0..1, fades in/out with movement   */
static f32 fp_synth_roll;             /* synthetic roll offset (degrees)    */
static f32 fp_prev_yaw;              /* previous frame yaw for turn rate   */
static f32 fp_saved_fov;             /* original FOV to restore on exit    */

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static f32 fp_clamp(f32 val, f32 lo, f32 hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

static f32 fp_atan2_deg(f32 y, f32 x) {
    /* Quarter-circle polynomial atan approximation, good to ~0.3 degrees */
    f32 abs_x = (x < 0.0f) ? -x : x;
    f32 abs_y = (y < 0.0f) ? -y : y;
    f32 a, s, r;

    if (abs_x < 0.0001f && abs_y < 0.0001f)
        return 0.0f;

    if (abs_x >= abs_y) {
        a = abs_y / abs_x;
        s = a * a;
        r = ((-12.88f * s + 56.85f) * s - 0.09f) * a;
    } else {
        a = abs_x / abs_y;
        s = a * a;
        r = 90.0f - ((-12.88f * s + 56.85f) * s - 0.09f) * a;
    }

    if (x < 0.0f) r = 180.0f - r;
    if (y < 0.0f) r = -r;
    return r;
}

/* Geometric roll from arm bone positions (works during normal walking/tilting) */
static f32 fp_get_body_roll(void) {
    f32 left[3], right[3];
    f32 dy, dx, dz, horiz;

    baModel_80291A50(BONE_LEFT_ARM, left);
    baModel_80291A50(BONE_RIGHT_ARM, right);

    dy = left[1] - right[1];
    dx = left[0] - right[0];
    dz = left[2] - right[2];
    horiz = gu_sqrtf(dx * dx + dz * dz);

    return fp_atan2_deg(dy, horiz);
}

/* Geometric pitch from head vs body bone (works during crouching/sliding) */
static f32 fp_get_body_pitch(void) {
    f32 head[3], body[3];
    f32 dy, dx, dz, forward;
    f32 sin_yaw, cos_yaw;

    baModel_80291A50(BONE_HEAD, head);
    baModel_80291A50(BONE_BODY, body);

    dy = head[1] - body[1];
    dx = head[0] - body[0];
    dz = head[2] - body[2];

    /* Project horizontal displacement onto player's forward direction */
    sin_yaw = ml_sin_deg(fp_yaw);
    cos_yaw = ml_cos_deg(fp_yaw);
    forward = dx * sin_yaw + dz * cos_yaw;

    return -fp_atan2_deg(forward, dy);
}

/* Synthetic vertical bob for pumpkin */
static f32 fp_synthetic_bob(f32 dt) {
    f32 freq, amp;
    s32 moving = (bastick_getZone() > 0);

    if (moving) {
        freq = FP_SYNTH_BOB_PUMPKIN_WALK;
        amp  = FP_SYNTH_BOB_PUMPKIN_AMP;
    } else {
        freq = FP_SYNTH_BOB_PUMPKIN_IDLE;
        amp  = FP_SYNTH_BOB_PUMPKIN_IDLE_AMP;
    }

    /* Ramp strength toward 1 */
    fp_bob_strength += (1.0f - fp_bob_strength) * 8.0f * dt;
    if (fp_bob_strength > 1.0f) fp_bob_strength = 1.0f;

    /* Advance phase */
    fp_bob_phase += freq * dt;
    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;

    return ml_sin_deg(fp_bob_phase) * amp * fp_bob_strength;
}

static void fp_enter(void) {
    f32 rot[3];

    fp_active = 1;
    fp_saved_fov = viewport_getFOVy();

    /* Initialise yaw from player facing direction */
    fp_yaw = player_getYaw();

    /* Grab current camera pitch so the transition isn't jarring */
    viewport_getRotation_vec3f(rot);
    fp_pitch = rot[0];
    if (fp_pitch > 180.0f) fp_pitch -= 360.0f;

    fp_last_map = map_get();
    fp_last_transformation = player_getTransformation();

    if (!recomp_get_config_u32("head_tracking"))
        player_setModelVisible(0);

    mouse_set_enabled(1);
}

static void fp_exit(void) {
    fp_active = 0;
    player_setModelVisible(1);
    viewport_setFOVy(fp_saved_fov);
    mouse_set_enabled(0);
}

/* ------------------------------------------------------------------ */
/* Safety: auto-exit first person when the situation changes           */
/* ------------------------------------------------------------------ */

static s32 fp_should_auto_exit(void) {
    if (map_get() != fp_last_map)
        return 1;
    if (player_getWaterState() != 0)
        return 1;
    if (player_getTransformation() != fp_last_transformation)
        return 1;
    if (player_isDead())
        return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* RECOMP_CALLBACK — one-time initialisation                           */
/* ------------------------------------------------------------------ */

RECOMP_CALLBACK("*", recomp_on_init) void on_init(void) {
    fp_active              = 0;
    fp_yaw                 = 0.0f;
    fp_pitch               = 0.0f;
    fp_last_map            = 0;
    fp_last_transformation = 0;
    fp_prev_toggle_held    = 0;
    fp_smooth_y            = 0.0f;
    fp_smooth_roll         = 0.0f;
    fp_bob_phase           = 0.0f;
    fp_bob_strength        = 0.0f;
    fp_synth_roll          = 0.0f;
    fp_prev_yaw            = 0.0f;
    fp_saved_fov           = 0.0f;
}

/* ------------------------------------------------------------------ */
/* PATCH — replace bainput_should_look_first_person_camera             */
/* ------------------------------------------------------------------ */

RECOMP_PATCH int bainput_should_look_first_person_camera(void) {
    if (fp_active)
        return 0;
    return bakey_pressed(BUTTON_C_UP) && can_view_first_person();
}

/* ------------------------------------------------------------------ */
/* HOOK (before) — ncDynamicCamera_update                              */
/* ------------------------------------------------------------------ */

RECOMP_HOOK("ncDynamicCamera_update") void before_camera_update(void) {
    /* --- FP toggle detection (rising edge of D-pad Up) --- */
    s32 toggle_held = bakey_held(BUTTON_D_UP);
    s32 toggle_just_pressed = toggle_held && !fp_prev_toggle_held;
    fp_prev_toggle_held = toggle_held;

    if (toggle_just_pressed) {
        if (!fp_active) {
            /* Allow entry during flight even if can_view_first_person() says no */
            s32 state = bs_getState();
            s32 in_flight = (state == BS_FLY || state == BS_BOMB
                          || state == BS_BEE_FLY);
            if (can_view_first_person() || in_flight) {
                fp_enter();
            }
        } else {
            fp_exit();
        }
    }

}

/* ------------------------------------------------------------------ */
/* HOOK_RETURN (after) — ncDynamicCamera_update                        */
/* ------------------------------------------------------------------ */

RECOMP_HOOK_RETURN("ncDynamicCamera_update") void after_camera_update(void) {
    f32 eye_pos[3];
    f32 rotation[3];
    f32 dt;
    s32 head_tracking;
    f32 cfg_fov, cfg_banjo_height, cfg_banjo_fwd;
    f32 cfg_trot_height, cfg_trot_fwd, cfg_flight_height, cfg_flight_fwd;
    f32 cfg_termite_height, cfg_termite_fwd, cfg_pumpkin_height, cfg_pumpkin_fwd;
    f32 cfg_croc_height, cfg_croc_fwd, cfg_walrus_height, cfg_walrus_fwd;
    f32 cfg_bee_height, cfg_bee_fwd;
    f32 cfg_boots_height, cfg_boots_fwd;
    f32 cfg_banjo_bob_amount;
    f32 cfg_banjo_roll;
    f32 cfg_banjo_pitch;
    f32 cfg_mouse_sens_x, cfg_mouse_sens_y;
    s32 cfg_mouse_invert_y, cfg_mouse_enabled;

    if (!fp_active)
        return;

    head_tracking = (s32)recomp_get_config_u32("head_tracking");

    /* --- read per-form config sliders --- */
    cfg_fov            = (f32)recomp_get_config_double("fov");
    cfg_banjo_height   = (f32)recomp_get_config_double("banjo_height");
    cfg_banjo_fwd      = (f32)recomp_get_config_double("banjo_forward");
    cfg_trot_height    = (f32)recomp_get_config_double("trot_height");
    cfg_trot_fwd       = (f32)recomp_get_config_double("trot_forward");
    cfg_flight_height  = (f32)recomp_get_config_double("flight_height");
    cfg_flight_fwd     = (f32)recomp_get_config_double("flight_forward");
    cfg_termite_height = (f32)recomp_get_config_double("termite_height");
    cfg_termite_fwd    = (f32)recomp_get_config_double("termite_forward");
    cfg_pumpkin_height = (f32)recomp_get_config_double("pumpkin_height");
    cfg_pumpkin_fwd    = (f32)recomp_get_config_double("pumpkin_forward");
    cfg_croc_height    = (f32)recomp_get_config_double("croc_height");
    cfg_croc_fwd       = (f32)recomp_get_config_double("croc_forward");
    cfg_walrus_height  = (f32)recomp_get_config_double("walrus_height");
    cfg_walrus_fwd     = (f32)recomp_get_config_double("walrus_forward");
    cfg_bee_height     = (f32)recomp_get_config_double("bee_height");
    cfg_bee_fwd        = (f32)recomp_get_config_double("bee_forward");
    cfg_boots_height   = (f32)recomp_get_config_double("boots_height");
    cfg_boots_fwd      = (f32)recomp_get_config_double("boots_forward");
    cfg_banjo_bob_amount = (f32)recomp_get_config_double("banjo_bob");
    cfg_banjo_roll       = (f32)recomp_get_config_double("banjo_roll");
    cfg_banjo_pitch      = (f32)recomp_get_config_double("banjo_pitch");

    cfg_mouse_sens_x   = (f32)recomp_get_config_double("mouse_sensitivity_x");
    cfg_mouse_sens_y   = (f32)recomp_get_config_double("mouse_sensitivity_y");
    cfg_mouse_invert_y = (s32)recomp_get_config_u32("mouse_invert_y");
    cfg_mouse_enabled  = (s32)recomp_get_config_u32("mouse_enabled");

    /* --- safety checks --- */
    if (fp_should_auto_exit()) {
        fp_exit();
        return;
    }

    /* --- look rotation from right stick (C-buttons) --- */
    dt = time_getDelta();

    {
        s32 classic = (s32)recomp_get_config_u32("camera_mode"); /* 0=Strafe, 1=Classic */
        s32 fly_state = bs_getState();
        s32 in_flight = (player_getTransformation() == TRANSFORM_BEE && fly_state == BS_BEE_FLY)
                     || fly_state == BS_FLY || fly_state == BS_BOMB;

        if (classic || in_flight) {
            /* Classic / flight: camera yaw locked to player facing direction */
            fp_yaw = player_getYaw();
        } else {
            /* Strafe: free horizontal look */
            if (bakey_held(BUTTON_C_LEFT))
                fp_yaw += FP_LOOK_SPEED * dt;
            if (bakey_held(BUTTON_C_RIGHT))
                fp_yaw -= FP_LOOK_SPEED * dt;
        }

        /* Vertical look (both modes) — suppress during egg-firing */
        if (fly_state != BS_EGG_HEAD && fly_state != BS_EGG_ASS) {
            if (bakey_held(BUTTON_C_UP))
                fp_pitch -= FP_LOOK_SPEED * dt;
            if (bakey_held(BUTTON_C_DOWN))
                fp_pitch += FP_LOOK_SPEED * dt;
        }

        /* Mouse look (additive with C-buttons) */
        if (cfg_mouse_enabled) {
            mouse_poll();
            if (mouse_is_captured()) {
                f32 mx = (f32)mouse_get_delta_x();
                f32 my = (f32)mouse_get_delta_y();
                f32 sx = cfg_mouse_sens_x * 0.022f;
                f32 sy = cfg_mouse_sens_y * 0.022f;
                if (!(classic || in_flight))
                    fp_yaw -= mx * sx;
                if (fly_state != BS_EGG_HEAD && fly_state != BS_EGG_ASS)
                    fp_pitch += (cfg_mouse_invert_y ? -my : my) * sy;
            }
        }
    }

    fp_yaw   = mlNormalizeAngle(fp_yaw);
    fp_pitch = fp_clamp(fp_pitch, FP_PITCH_MIN, FP_PITCH_MAX);

    /* --- model visibility (game re-enables it each frame) --- */
    if (!head_tracking)
        player_setModelVisible(0);

    /* --- align player to camera during egg states so eggs fire where you look --- */
    {
        s32 state = bs_getState();
        if (state == BS_EGG_HEAD || state == BS_EGG_ASS) {
            yaw_set(fp_yaw);
            yaw_setIdeal(fp_yaw);
        }
    }

    /* --- compute eye position --- */
    if (head_tracking) {
        f32 alpha;
        s32 uses_synth_bob = 0;
        s32 uses_synth_sway = 0;
        s32 uses_bone_y = 0;
        f32 smooth_speed = FP_BOB_SMOOTH;
        u32 xform = player_getTransformation();
        f32 player_pos[3];

        player_getPosition(player_pos);

        if (xform == TRANSFORM_BEE) {
            /* Bee: no usable bones, use player pos + offset */
            player_getPosition(eye_pos);
            eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_bee_fwd;
            eye_pos[1] += cfg_bee_height;
            eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_bee_fwd;
            uses_synth_sway = 1;
        } else if (xform == TRANSFORM_PUMPKIN) {
            /* Pumpkin: no usable bones, use player pos + offset */
            player_getPosition(eye_pos);
            eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_pumpkin_fwd;
            eye_pos[1] += cfg_pumpkin_height;
            eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_pumpkin_fwd;
            uses_synth_bob = 1;
        } else {
            baModel_802924E8(eye_pos);           /* animated head bone (X/Z tracking) */

            if (xform == TRANSFORM_TERMITE) {
                eye_pos[1] = player_pos[1] + cfg_termite_height;
                eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_termite_fwd;
                eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_termite_fwd;
                uses_synth_sway = 1;
            } else if (xform == TRANSFORM_WASHUP) {
                eye_pos[1] += FP_EYE_Y_BOOST + 95.0f;
                eye_pos[0] += ml_sin_deg(fp_yaw) * 60.0f;
                eye_pos[2] += ml_cos_deg(fp_yaw) * 60.0f;
                uses_synth_sway = 1;
                uses_bone_y = 1;
            } else if (xform == TRANSFORM_CROC) {
                eye_pos[1] = player_pos[1] + cfg_croc_height;
                eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_croc_fwd;
                eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_croc_fwd;
            } else if (xform == TRANSFORM_WALRUS) {
                /* Forward + lateral offset to align with walrus face */
                f32 left = -10.0f;
                eye_pos[1] = player_pos[1] + cfg_walrus_height;
                eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_walrus_fwd - ml_cos_deg(fp_yaw) * left;
                eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_walrus_fwd + ml_sin_deg(fp_yaw) * left;
            } else {
                s32 st = bs_getState();
                s32 in_trot = (st == BS_BTROT_IDLE || st == BS_BTROT_WALK
                            || st == BS_BTROT_JUMP || st == BS_BTROT_SLIDE);
                s32 in_boots = (st == BS_LONGLEG_IDLE || st == BS_LONGLEG_WALK
                             || st == BS_LONGLEG_JUMP || st == BS_LONGLEG_SLIDE);
                if (in_boots) {
                    /* Wading boots: relative offset from bone */
                    eye_pos[1] += FP_EYE_Y_BOOST + cfg_boots_height;
                    eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_boots_fwd;
                    eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_boots_fwd;
                    uses_bone_y = 1;
                } else if (in_trot) {
                    /* Kazooie's head: relative offset from bone */
                    eye_pos[1] += FP_EYE_Y_BOOST + cfg_trot_height;
                    eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_trot_fwd;
                    eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_trot_fwd;
                    uses_synth_sway = 1;
                    uses_bone_y = 1;
                } else if (st == BS_FLY || st == BS_BOMB) {
                    /* Flying: relative offset from bone */
                    eye_pos[1] += FP_EYE_Y_BOOST + cfg_flight_height;
                    eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_flight_fwd;
                    eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_flight_fwd;
                    uses_bone_y = 1;
                } else {
                    /* Banjo default: bone-tracked with configurable smoothing */
                    eye_pos[1] += FP_EYE_Y_BOOST + 5.0f;
                    eye_pos[0] += ml_sin_deg(fp_yaw) * cfg_banjo_fwd;
                    eye_pos[2] += ml_cos_deg(fp_yaw) * cfg_banjo_fwd;
                    uses_bone_y = 1;
                    smooth_speed = cfg_banjo_bob_amount;
                }
            }
        }

        /* Smooth Y to dampen walk-cycle bobbing (bone-tracked Y forms only).
         * Forms using absolute player-position height don't need smoothing. */
        if (uses_bone_y) {
            if (fp_smooth_y == 0.0f)
                fp_smooth_y = eye_pos[1];        /* seed on first frame */
            alpha = smooth_speed * dt;
            if (alpha > 1.0f) alpha = 1.0f;
            fp_smooth_y += (eye_pos[1] - fp_smooth_y) * alpha;
            eye_pos[1] = fp_smooth_y;
        }

        /* Apply synthetic bob AFTER smoothing so the filter doesn't eat it */
        if (uses_synth_bob)
            eye_pos[1] += fp_synthetic_bob(dt);

        /* Synthetic sway (termite + bee + trot) */
        if (uses_synth_sway) {
            s32 moving = (bastick_getZone() > 0);
            if (xform == TRANSFORM_TERMITE) {
                if (moving) {
                    /* Walking: vertical bob like pumpkin walk */
                    fp_bob_phase += FP_SYNTH_BOB_PUMPKIN_WALK * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    eye_pos[1] += ml_sin_deg(fp_bob_phase) * FP_SYNTH_BOB_PUMPKIN_AMP;
                } else {
                    /* Idle: side-to-side sway with downward dip in middle */
                    f32 sway;
                    fp_bob_phase += FP_TERMITE_SWAY_FREQ * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    sway = ml_sin_deg(fp_bob_phase) * FP_TERMITE_SWAY_HORIZ;
                    eye_pos[0] += -ml_cos_deg(fp_yaw) * sway;
                    eye_pos[2] +=  ml_sin_deg(fp_yaw) * sway;
                    eye_pos[1] -= (ml_cos_deg(2.0f * fp_bob_phase) + 1.0f) * 0.5f * FP_TERMITE_SWAY_DIP;
                }
            } else if (xform == TRANSFORM_BEE || xform == TRANSFORM_WASHUP) {
                if (moving && xform == TRANSFORM_BEE) {
                    /* Bee walking: roll (body dip side to side) */
                    fp_bob_phase += FP_BEE_WALK_FREQ * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    fp_synth_roll = ml_sin_deg(fp_bob_phase) * FP_BEE_WALK_ROLL;
                } else if (moving && xform == TRANSFORM_WASHUP) {
                    /* Washup walking: side-to-side sway with upward arc in middle */
                    f32 sway;
                    fp_bob_phase += FP_WASHUP_WALK_FREQ * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    sway = ml_sin_deg(fp_bob_phase) * FP_WASHUP_SWAY_HORIZ;
                    eye_pos[0] += -ml_cos_deg(fp_yaw) * sway;
                    eye_pos[2] +=  ml_sin_deg(fp_yaw) * sway;
                    eye_pos[1] += (ml_cos_deg(2.0f * fp_bob_phase) + 1.0f) * 0.5f * FP_WASHUP_SWAY_VERT;
                } else if (!moving) {
                    /* Idle: asymmetric harmonic sway — double-left, single-right */
                    f32 sway;
                    fp_bob_phase += FP_BEE_IDLE_FREQ * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    sway = (ml_sin_deg(fp_bob_phase)
                          + FP_BEE_IDLE_HARMONIC * ml_sin_deg(3.0f * fp_bob_phase))
                         * (xform == TRANSFORM_WASHUP ? FP_WASHUP_IDLE_SWAY : FP_BEE_IDLE_SWAY);
                    eye_pos[0] += -ml_cos_deg(fp_yaw) * sway;
                    eye_pos[2] +=  ml_sin_deg(fp_yaw) * sway;
                }
            } else {
                /* Talon trot: vertical bob when running */
                if (moving) {
                    fp_bob_phase += FP_TROT_BOB_FREQ * dt;
                    if (fp_bob_phase >= 360.0f) fp_bob_phase -= 360.0f;
                    eye_pos[1] += ml_sin_deg(fp_bob_phase) * FP_TROT_BOB_AMP;
                }
            }
        }
    } else {
        u32 xform = player_getTransformation();
        player_getPosition(eye_pos);
        switch (xform) {
            case TRANSFORM_TERMITE: eye_pos[1] += cfg_termite_height; break;
            case TRANSFORM_PUMPKIN: eye_pos[1] += cfg_pumpkin_height; break;
            case TRANSFORM_CROC:    eye_pos[1] += cfg_croc_height;    break;
            case TRANSFORM_WALRUS:  eye_pos[1] += cfg_walrus_height;  break;
            case TRANSFORM_BEE:     eye_pos[1] += cfg_bee_height;     break;
            case TRANSFORM_WASHUP:  eye_pos[1] += 150.0f;             break;
            default:                eye_pos[1] += cfg_banjo_height;    break;
        }
    }

    /* --- apply to viewport --- */
    {
        s32 fly_st = bs_getState();
        s32 bee_flying = (player_getTransformation() == TRANSFORM_BEE
                          && fly_st == BS_BEE_FLY);
        s32 banjo_flying = (fly_st == BS_FLY || fly_st == BS_BOMB);
        f32 model_pitch = pitch_get();
        /* Convert 0-360 range to signed ±180 */
        if (model_pitch > 180.0f) model_pitch -= 360.0f;

        if (bee_flying || banjo_flying) {
            /* Flight: follow model pitch (inverted) */
            rotation[0] = fp_pitch - model_pitch;
        } else if (head_tracking) {
            if (model_pitch > 10.0f || model_pitch < -10.0f)
                rotation[0] = fp_pitch + model_pitch;   /* rolls, flips, slides */
            else
                rotation[0] = fp_pitch + fp_clamp(fp_get_body_pitch(),
                                                   -cfg_banjo_pitch, cfg_banjo_pitch);
        } else {
            rotation[0] = fp_pitch;
        }
    }

    {
        s32 state = bs_getState();
        if (state == BS_EGG_ASS)
            rotation[1] = mlNormalizeAngle(fp_yaw);        /* reverse view */
        else
            rotation[1] = mlNormalizeAngle(fp_yaw + 180.0f);
    }

    {
        s32 fly_st2 = bs_getState();
        s32 bee_fly = (player_getTransformation() == TRANSFORM_BEE
                       && fly_st2 == BS_BEE_FLY);
        s32 banjo_fly = (fly_st2 == BS_FLY || fly_st2 == BS_BOMB);

        if (bee_fly || banjo_fly) {
            /* Flight: roll based on yaw turn rate */
            f32 yaw_delta = fp_yaw - fp_prev_yaw;
            if (yaw_delta > 180.0f) yaw_delta -= 360.0f;
            if (yaw_delta < -180.0f) yaw_delta += 360.0f;
            f32 turn_rate = (dt > 0.0001f) ? (yaw_delta / dt) : 0.0f;
            f32 target_roll = fp_clamp(-turn_rate * FP_FLIGHT_ROLL_SCALE,
                                        -FP_FLIGHT_ROLL_MAX, FP_FLIGHT_ROLL_MAX);
            f32 roll_alpha = FP_BOB_SMOOTH * dt;
            if (roll_alpha > 1.0f) roll_alpha = 1.0f;
            fp_smooth_roll += (target_roll - fp_smooth_roll) * roll_alpha;
            rotation[2] = fp_smooth_roll + fp_synth_roll;
        } else if (head_tracking) {
            f32 target_roll = fp_clamp(fp_get_body_roll(), -cfg_banjo_roll, cfg_banjo_roll);
            f32 roll_alpha = FP_BOB_SMOOTH * dt;
            if (roll_alpha > 1.0f) roll_alpha = 1.0f;
            fp_smooth_roll += (target_roll - fp_smooth_roll) * roll_alpha;
            rotation[2] = fp_smooth_roll + fp_synth_roll;
        } else {
            fp_smooth_roll = 0.0f;
            rotation[2] = fp_synth_roll;
        }
    }
    fp_synth_roll = 0.0f;
    fp_prev_yaw = fp_yaw;

    viewport_setPosition_vec3f(eye_pos);
    viewport_setRotation_vec3f(rotation);
    viewport_setFOVy(cfg_fov);
}
