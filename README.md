# BK First Person Mode

A mod for Banjo-Kazooie Recompiled that adds a toggled first-person camera with full movement control.


## Changelog

### 0.5.0

- First-person swimming (surface and underwater)
- Auto-restore first person after going through doors / map transitions
- Fixed camera being low when using Talon Trot on steep hills

### 0.4.0

- Mouse look support (Linux & Windows) with configurable sensitivity and invert Y
- Cursor automatically released during pause and recomp menus
- Windows native library (.dll) support

### 0.3.1

- Default Banjo forward camera to 15

### 0.3.0
- Configurable FOV (30-120)
- Wading boots camera support
- Changed Talon Trot camera position
- Banjo head bob, roll, and pitch sliders if you hate how much Banjo's head move around

## Features

- **D-pad Up**: Toggle first-person mode on/off
- **C-buttons / Right stick**: Look around
- **Left stick**: Move
- **Mouse look**: Tested on Linux. Needs testing on Windows

### Camera Modes

- **Strafe** (default): Camera direction is independent, player can strafe
- **Classic**: Camera follows the player's facing direction. This is very jerky and incredibly difficult to play with. 180 degree turns don't really work.

### Head Tracking

- **Off** (default): Static camera at eye level, player model hidden
- **On**: Camera follows animated head bone with geometric pitch/roll, player model visible

### FOV

Adjustable field of view from 30 to 120 degrees (default 60). Original FOV is restored when exiting first-person mode.

### Per-Form Camera Sliders

Each transformation has configurable **height** and **forward offset** sliders in the mod settings menu, allowing fine-tuning of camera placement per form. Defaults are shown in the slider names for easy reference.

All configurable in the mod settings menu.

### Per-Form Support

Custom camera positioning and synthetic animations for all transformations:

- **Banjo**: Bone-tracked head with geometric pitch/roll
- **Talon Trot**: Camera at Kazooie's head with running bob
- **Flight**: Yaw and pitch locked to player direction
- **Termite**: Idle side-to-side sway, walking vertical bob
- **Pumpkin**: Idle hop and walking bob
- **Crocodile**: Bone-tracked with forward offset
- **Walrus**: Bone-tracked with directional offset
- **Bee**: Asymmetric idle sway, walking roll tilt, flight locks camera to head.
- **Washing Machine**: Bone-tracked with idle sway and walking sway with upward arc
- **Surface Swimming**: Head bone-tracked with free look
- **Underwater**: Player-position based with spring-back camera

### Notes

Automatically exits first-person when:
- Changing maps (auto-restores after the transition)
- Transforming
- Dying

You can see parts of the character model when moving independently of the camera.

## Quirks and Technical Notes

- Strafing works because the mod never enters the game's built-in first-person state, which freezes movement. Instead, it patches `bainput_should_look_first_person_camera` to return 0 while active, letting the normal player state machine (walk/run/idle) continue running. The camera yaw is tracked independently from the player's facing direction, so the left stick moves the player relative to the camera while the player model turns freely. This has a side effect of the character if often not facing the camera's direction.
- Egg shooting aligns Banjo's facing direction to the camera so eggs fire where you're looking. You may see Banjo orient himself right before shooting eggs.
- The game re-enables the player model every frame, so in non-head-tracking mode the mod must hide it each frame.
- Bone tracking (`baModel_802924E8`) does not work for the bee or pumpkin transformations. The function returns stale/zero data for these forms. They use synthetic animation instead with roughly in time with the animations.
- The termite's bone tracking returns a mid-body position rather than the head, so a large forward offset and synthetic head bobbing are needed.
- The walrus head bone is offset to the right of center, requiring a lateral correction, I think. The placing the camera 180 deg shows its offcenter but when looking forward, it isn't. Its currently at -10 but I'm not sure the impact.
- Banjo's original head bobbing locked to his skeleton was so violent, it was unusable. The roll and pitch was very harsh.
- The bee's idle animation has an asymmetric sway pattern (double/triple-left bounce, single-right). I think this is the only transformation with an asymmetrical animation.
- During flight (both bee and Banjo), the camera control inverts to match the flight controls.
- `player_getWaterState()` stays non-zero after the player visually leaves water. Swimming detection requires both `player_getWaterState() != 0` AND an active swim animation state to avoid getting stuck in swimming camera mode.
- The player model must stay visible during swimming (with head tracking on) or the bone/animation system freezes and doesn't recover until FP is toggled off and on.





## TODO

- Locked first person? Not sure if this is possible.
- More testing in different situations

## Acknowledgments

Thanks to ProxyBK for the TransformAnywhere mod, which substantially decreased development time.

## Building

Requires `clang`, `ld.lld`, and `RecompModTool` in the project root.

```
git submodule update --init --recursive
make
```

The built mod will be at `build/bk_first_person_mode.nrm`.
