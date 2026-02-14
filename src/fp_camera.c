#include "modding.h"
#include "recomputils.h"
#include "PR/ultratypes.h"

/* ------------------------------------------------------------------ */
/* Base game function declarations (resolved via syms.toml)            */
/* ------------------------------------------------------------------ */

int  bakey_pressed(s32 button);
u32  bakey_held(s32 button);
int  can_view_first_person(void);
void player_getPosition(f32 dst[3]);
void func_8028E9C4(s32 arg0, f32 dst[3]);
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

/* Player model rotation (degrees, used by renderer — captures full rolls/flips) */
f32  pitch_get(void);

/* ------------------------------------------------------------------ */
/* Button constants (from enums.h)                                    */
/* ------------------------------------------------------------------ */

#define BUTTON_D_UP    0x4
#define BUTTON_D_DOWN  0x5
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

/* ------------------------------------------------------------------ */
/* Tuning constants                                                    */
/* ------------------------------------------------------------------ */

#define FP_LOOK_SPEED    120.0f   /* degrees per second                */
#define FP_PITCH_MIN    (-80.0f)  /* look-up limit                     */
#define FP_PITCH_MAX      80.0f   /* look-down limit                   */
#define FP_EYE_ARG        5       /* arg0 to func_8028E9C4 for eyes    */
#define FP_EYE_Y_BOOST   30.0f   /* extra height to reach eye level   */

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static s32 fp_active;
static s32 fp_head_tracking;             /* 0 = static offset, 1 = bone */
static f32 fp_yaw;
static f32 fp_pitch;
static s32 fp_last_map;
static u32 fp_last_transformation;
static s32 fp_prev_toggle_held;          /* for rising-edge detection    */
static s32 fp_prev_ht_held;             /* for head-tracking toggle     */

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

static void fp_enter(void) {
    f32 rot[3];

    fp_active = 1;

    /* Initialise yaw from player facing direction */
    fp_yaw = player_getYaw();

    /* Grab current camera pitch so the transition isn't jarring */
    viewport_getRotation_vec3f(rot);
    fp_pitch = rot[0];

    fp_last_map = map_get();
    fp_last_transformation = player_getTransformation();

    if (!fp_head_tracking)
        player_setModelVisible(0);
}

static void fp_exit(void) {
    fp_active = 0;
    player_setModelVisible(1);
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
    fp_head_tracking       = 0;
    fp_yaw                 = 0.0f;
    fp_pitch               = 0.0f;
    fp_last_map            = 0;
    fp_last_transformation = 0;
    fp_prev_toggle_held    = 0;
    fp_prev_ht_held        = 0;
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
            if (can_view_first_person()) {
                fp_enter();
            }
        } else {
            fp_exit();
        }
    }

    /* --- Head tracking toggle (rising edge of D-pad Down while in FP) --- */
    if (fp_active) {
        s32 ht_held = bakey_held(BUTTON_D_DOWN);
        s32 ht_just_pressed = ht_held && !fp_prev_ht_held;
        fp_prev_ht_held = ht_held;

        if (ht_just_pressed) {
            fp_head_tracking = !fp_head_tracking;
            player_setModelVisible(fp_head_tracking ? 1 : 0);
        }
    } else {
        fp_prev_ht_held = 0;
    }
}

/* ------------------------------------------------------------------ */
/* HOOK_RETURN (after) — ncDynamicCamera_update                        */
/* ------------------------------------------------------------------ */

RECOMP_HOOK_RETURN("ncDynamicCamera_update") void after_camera_update(void) {
    f32 eye_pos[3];
    f32 rotation[3];
    f32 dt;

    if (!fp_active)
        return;

    /* --- safety checks --- */
    if (fp_should_auto_exit()) {
        fp_exit();
        return;
    }

    /* --- look rotation from right stick (C-buttons) --- */
    dt = time_getDelta();

    if (bakey_held(BUTTON_C_LEFT))
        fp_yaw += FP_LOOK_SPEED * dt;
    if (bakey_held(BUTTON_C_RIGHT))
        fp_yaw -= FP_LOOK_SPEED * dt;

    /* Suppress pitch during egg-firing states so C-Up/C-Down don't jerk the camera */
    {
        s32 state = bs_getState();
        if (state != BS_EGG_HEAD && state != BS_EGG_ASS) {
            if (bakey_held(BUTTON_C_UP))
                fp_pitch -= FP_LOOK_SPEED * dt;
            if (bakey_held(BUTTON_C_DOWN))
                fp_pitch += FP_LOOK_SPEED * dt;
        }
    }

    fp_yaw   = mlNormalizeAngle(fp_yaw);
    fp_pitch = fp_clamp(fp_pitch, FP_PITCH_MIN, FP_PITCH_MAX);

    /* --- model visibility (game re-enables it each frame) --- */
    if (!fp_head_tracking)
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
    if (fp_head_tracking)
        baModel_802924E8(eye_pos);           /* animated head bone */
    else
        func_8028E9C4(FP_EYE_ARG, eye_pos); /* static height offset */
    eye_pos[1] += FP_EYE_Y_BOOST;

    /* --- apply to viewport --- */
    if (fp_head_tracking) {
        f32 model_pitch = pitch_get();
        /* Convert 0-360 range to signed ±180 */
        if (model_pitch > 180.0f) model_pitch -= 360.0f;

        if (model_pitch > 10.0f || model_pitch < -10.0f)
            rotation[0] = fp_pitch + model_pitch;   /* rolls, flips, slides */
        else
            rotation[0] = fp_pitch + fp_get_body_pitch(); /* crouching, small tilts */
    } else {
        rotation[0] = fp_pitch;
    }

    {
        s32 state = bs_getState();
        if (state == BS_EGG_ASS)
            rotation[1] = mlNormalizeAngle(fp_yaw);        /* reverse view */
        else
            rotation[1] = mlNormalizeAngle(fp_yaw + 180.0f);
    }

    if (fp_head_tracking)
        rotation[2] = fp_get_body_roll();
    else
        rotation[2] = 0.0f;

    viewport_setPosition_vec3f(eye_pos);
    viewport_setRotation_vec3f(rotation);
}
