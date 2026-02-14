# BK First Person Mode

A mod for Banjo-Kazooie Recompiled that adds a toggled first-person camera with full movement control.

## Features

- **D-pad Up**: Toggle first-person mode on/off
- **C-buttons / Right stick**: Look around
- **Left stick**: Full movement retained in first-person

### Camera Modes

- **Strafe** (default): Camera direction is independent, player can strafe
- **Classic**: Camera follows the player's facing direction. This is very jerky and incredibly difficult to play with. 180 degree turns don't really work.

### Head Tracking

- **Off** (default): Static camera at eye level, player model hidden
- **On**: Camera follows animated head bone with geometric pitch/roll, player model visible

Configurable in the mod settings menu.

### Per-Form Support

Custom camera positioning and synthetic animations for all transformations:

- **Banjo**: Bone-tracked head with geometric pitch/roll
- **Talon Trot**: Camera at Kazooie's head with running bob
- **Flight**: Yaw and pitch locked to player direction
- **Termite**: Idle side-to-side sway, walking vertical bob
- **Pumpkin**: Idle hop and walking bob
- **Crocodile**: Bone-tracked with forward offset
- **Walrus**: Bone-tracked with directional offset
- **Bee**: Asymmetric idle sway, walking roll tilt, flight pitch follow
- **Washing Machine**: Bone-tracked with height/forward offset

### Safety

Automatically exits first-person when:
- Changing maps
- Entering water
- Transforming
- Dying

## Acknowledgments

Thanks to ProxyBK for the TransformAnywhere mod, which substantially decreased development time.

## Building

Requires `clang`, `ld.lld`, and `RecompModTool` in the project root.

```
git submodule update --init --recursive
make
```

The built mod will be at `build/bk_first_person_mode.nrm`.
