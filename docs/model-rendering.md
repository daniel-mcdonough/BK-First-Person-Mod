# BK Model Rendering & Partial Visibility Research

Research into selectively hiding body parts of the player model in first-person mode.

## Approaches

### 1. Near Clip Plane (Easiest)

`viewport_setNearAndFar(near, far)` — defaults 30.0/4000.0.

Push near plane forward to clip geometry close to the camera. Head/torso get clipped while arms/legs stay visible.

```c
// Declaration
void viewport_setNearAndFar(f32 near, f32 far);

// Located in bk-decomp/src/core1/viewport.c
// Header: bk-decomp/include/core1/viewport.h (line 27)
// Default: near=30.0f, far=4000.0f (viewport.c lines 14-15)
```

### 2. Segment Visibility Selector (CmdC System — Most Precise)

The rendering engine has a built-in part visibility system using geo command C.

- `D_80383658[0x2A]` — array of 42 slots controlling which model segments render
- Positive index: selects single geometry (indices 1 to cmd->unk8)
- Negative index: bit-mask for multiple parts (each bit = sub-part)
- Index 0 or command ID 0: part is hidden

```c
// Set segment visibility by index
void func_8033A45C(s32 slot, s32 index);     // show specific segment
void func_8033A470(s32 slot, s32 index);     // bit-mask mode (-index)

// Located in bk-decomp/src/core2/modelRender.c (lines 1491-1495)
```

Challenge: need to determine which slot indices map to which body parts (head, torso, arms, legs).

### 3. Bone Scale Manipulation

`BoneTransform` structs have a `scale[3]` field. Setting a bone's scale to zero collapses that part.

```c
// bk-decomp/include/core2/bonetransform.h
typedef struct {
    f32 unk0[4];
    f32 scale[3];
    f32 unk1C[3];
} BoneTransform;

typedef struct bone_transform_list_s {
    BoneTransform *ptr;
    s32 count;
} BoneTransformList;
```

Requires hooking into the bone transform pipeline via `modelRender_setBoneTransformList()`.

## Key Functions

### Player Model

| Function | Location | Purpose |
|----------|----------|---------|
| `baModel_draw(Gfx**, Mtx**, Vtx**)` | model.c:69 | Main player model draw |
| `baModel_setVisible(s32)` | model.c:265 | Overall model visibility |
| `baModel_setScale(f32)` | model.h | Model scale |
| `baModel_80291A50(s32 bone, f32 dst[3])` | model.c:54 | Get bone world position |
| `baModel_802924E8(f32 dst[3])` | model.h | Get head bone position |

### Bone Indices

From model.c lines 298-305:

| Index | Part |
|-------|------|
| 5 | Body |
| 7 | Right arm |
| 8 | Left arm |
| 9 | Head |
| 10 (0xA) | Wing/backpack |
| 11 (0xB) | Walrus tusk |

### Rendering Pipeline

| Function | Location | Purpose |
|----------|----------|---------|
| `modelRender_draw(...)` | modelRender.c:1010 | Core display list renderer |
| `modelRender_setBoneTransformList(...)` | modelRender.c:1393 | Set bone transforms |
| `func_80338CD0()` | modelRender.c:887 | CmdC segment visibility |
| `func_80338EB8()` | modelRender.c:947 | CmdE bone-based visibility |
| `func_8033909C()` | modelRender.c:988 | CmdF conditional visibility |
| `func_802ED420(...)` | code_66490.c | Test segment visibility flags |

### Viewport

| Function | Location | Purpose |
|----------|----------|---------|
| `viewport_setNearAndFar(f32, f32)` | viewport.h:27 | Near/far clip planes |
| `viewport_setRenderPerspectiveMatrix(...)` | viewport.c:90 | Perspective matrix with custom near/far |
| `viewport_setFOVy(f32)` | viewport.h:43 | Field of view |
| `viewport_getFOVy()` | viewport.h:42 | Get current FOV |

## Model Data Structures

```c
// bk-decomp/include/model.h
typedef struct {
    BKMeshList *meshList_0;
    BKVertexList *vtxList_4;
} BKModel;

typedef struct {
    u8 pad0[0x4];
    s32 geo_list_offset_4;
    s16 texture_list_offset_8;
    s16 geo_typ_A;
    s32 gfx_list_offset_C;
    s32 vtx_list_offset_10;
    s32 unk14;                          // possibly bone list
    s32 animation_list_offset_18;
    s32 collision_list_offset_1C;
    s32 unk20;                          // BKModelUnk20List - segment visibility
    s32 effects_list_setup_24;
    s32 unk28;                          // possible bone/vertex animation data
    s32 animated_texture_list_offset;
} BKModelBin;

// Segment visibility structure
typedef struct {
    s16 unk0[3];
    s16 unk6[3];
    u8 unkC;
} BKModelUnk20_0;

typedef struct {
    u8 unk0;
    // BKModelUnk20_0[]
} BKModelUnk20List;
```

## File Locations

- Player model control: `bk-decomp/include/core2/ba/model.h` + `bk-decomp/src/core2/ba/model.c`
- Rendering engine: `bk-decomp/src/core2/modelRender.c` + `bk-decomp/include/core2/modelRender.h`
- Segment visibility: `bk-decomp/src/core2/code_66490.c`
- Viewport/camera: `bk-decomp/src/core1/viewport.c` + `bk-decomp/include/core1/viewport.h`
- Bone transforms: `bk-decomp/include/core2/bonetransform.h`
- Model structures: `bk-decomp/include/model.h`
- Skeletal animation: `bk-decomp/include/core2/skeletalanim.h`
- Symbol table: `BanjoRecompSyms/bk.us.rev0.syms.toml`
