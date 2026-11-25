# openxr_loader.dll proxy

![screenshot](https://github.com/aruametello/openxr_loader-dll-proxy/blob/main/screenshot-2.png?raw=true)

A proxy DLL for the Khronos `openxr_loader.dll` ([OpenXR loader specification](https://registry.khronos.org/OpenXR/specs/1.0/loader.html)). It sits between a VR application and the OpenXR runtime so that controller input and pose output can be adjusted per game.

Video description and demonstration: https://www.youtube.com/watch?v=WixaeDdFIWg

Credits: based on the upstream [openxr_loader-dll-proxy](https://github.com/aruametello/openxr_loader-dll-proxy) project by aruametello. Built against the [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK).

## Requirements

- A Windows PC with a PC OpenXR runtime (for example SteamVR, Oculus/Meta PC, or Windows Mixed Reality).
- Standalone Quest titles are not supported. A PC OpenXR runtime is required.
- Use either the proxy DLL or the OpenXR API-layer build, never both at once. Running both causes conflicting input and pose changes.

## Building

The Makefile contains these targets:

- `dll_release`: builds the proxy DLL. This is the file to deploy.
- `dll_debug`: builds a debug variant.
- `input_layer`: builds the OpenXR API-layer variant.
- `clean`: removes build output.

Requirements:

1. Install [MSYS2](https://www.msys2.org/) and open the **UCRT64** shell.
2. Install the toolchain and make:

   ```
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc make
   ```

3. Install the OpenXR SDK package (headers and loader):

   ```
   pacman -S mingw-w64-ucrt-x86_64-openxr
   ```

   Alternatively, download the Khronos OpenXR SDK and pass its paths to the Makefile (see below).

4. From the repository root, build:

   ```
   make dll_release
   make input_layer
   ```

If the OpenXR SDK is not in the default location, pass its include and library paths:

```
make dll_release OPENXR_INCLUDE="/ucrt64/include" OPENXR_LIB="/ucrt64/lib"
```

The precompiled binaries were built with MSYS2 (UCRT64/MinGW-w64 GCC). Other GCC-compatible toolchains are likely to work.

### Offline trigger core tests

These tests need no VR runtime:

```
gcc -std=c11 -Wall -Isrc tools/trigger_curve_test.c src/xr_core.c src/Quaternion.c -o trigger_curve_test.exe -lm
trigger_curve_test.exe
```

The last line reports `OVERALL PASS (0 failure(s))` when every check passes.

## Configuration

Settings are read from `openxr_loader_config.txt` in the folder containing the DLL. Rotation values are in degrees, positions in metres. Use `.` as the decimal point. Lines starting with `#` are comments.

### Pose offsets

| Key | Range / default | Meaning |
|---|---|---|
| `left_hand_rotation_offset_x` | degrees, default 0 | Left controller pitch offset (`x` = up/down). |
| `left_hand_rotation_offset_y` | degrees, default 0 | Left controller yaw offset (`y` = left/right). |
| `left_hand_rotation_offset_z` | degrees, default 0 | Left controller roll offset (`z` = twist). |
| `right_hand_rotation_offset_x` | degrees, default 0 | Right controller pitch offset. |
| `right_hand_rotation_offset_y` | degrees, default 0 | Right controller yaw offset. |
| `right_hand_rotation_offset_z` | degrees, default 0 | Right controller roll offset. |
| `left_hand_position_offset_x` | metres, default 0 | Left controller position offset along x. |
| `left_hand_position_offset_y` | metres, default 0 | Left controller position offset along y. |
| `left_hand_position_offset_z` | metres, default 0 | Left controller position offset along z. |
| `right_hand_position_offset_x` | metres, default 0 | Right controller position offset along x. |
| `right_hand_position_offset_y` | metres, default 0 | Right controller position offset along y. |
| `right_hand_position_offset_z` | metres, default 0 | Right controller position offset along z. |
| `frame` | `local` or `world`, default `world` | Reference frame for the right-hand offset. Invalid values are rejected and the file is rewritten. |
| `capture_ads_apply_position` | `0` or `1`, default `0` | When `1`, ADS capture also stores position, not only rotation. |

The shipped `openxr_loader_config.txt` contains example rotation values for both hands. They are a sample, not the built-in defaults.

### Input and remapping

| Key | Range / default | Meaning |
|---|---|---|
| `binding_log` | 0-2, default 1 | Binding log level. Out-of-range values reset to 1. |
| `remap_enabled` | `0` or `1`, default `0` | Enables action-binding remapping rules. |
| `profile_process` | executable name, default `readyornot-win64-shipping.exe` | Process whose interaction profiles are remapped. |

### Trigger settings

The analog pull (`trigger/value`) and the digital click (`trigger/click`, or the legacy boolean action) are separate signals controlled by different keys. Analog shaping keys change only the analog value. Click keys decide whether and how a synthesized click combines with the runtime click. Check which signal your game reads before tuning.

| Key | Range / default | Meaning |
|---|---|---|
| `trigger_press_threshold` | 0-1 | Boolean press threshold applied to the curved analog value. |
| `trigger_deadzone` | 0-0.9, default 0 | Analog output is exactly 0.0 below this pull value. |
| `trigger_curve_exponent` | 0.01-20, default 1 | Shapes the analog response. Values above about 3 saturate the axis quickly. |
| `trigger_gain` | 0-100, default 1 | Scales the analog value after the curve; the result is clamped to [0, 1]. |
| `trigger_range_max` | (0, 1], default 1 | Raw pull is divided by this value before deadzone and curve. Invalid values fall back to 1.0 and a warning is logged. |
| `trigger_force_value` | -1 or 0-1, default -1 | -1 disables forcing. 0-1 delivers that constant as the trigger analog value. |
| `trigger_click_threshold` | -1 or 0-1, default -1 | -1 passes through the hardware click. 0-1 synthesizes a click when the value reaches the threshold. |
| `click_mode` | `replace`, `or`, `and`; default `replace` | How a synthesized click combines with the runtime click. `replace` ignores the runtime click; `or` fires if either fires; `and` fires only if both fire. |
| `click_compare` | `rawpull` or `curved`; default `rawpull` | Value compared against `trigger_click_threshold`. `rawpull` uses the value after deadzone and range normalization, before exponent and gain. `curved` uses the value after exponent and gain. |
| `trigger_target_action` | default empty | With two or more actions bound to `trigger/value`, applies shaping only to the named action. |

## Findings: Ready or Not VRO (tested)

These results were observed in Ready or Not with VRO.

- **UE4 legacy trigger click path:** the title reads the trigger click from the UE4 legacy boolean action rather than the analog axis.
- **Four duplicate profile actions:** the profile binds four actions that map to the same trigger input, so remapping must treat them as a group.
- **Separate analog and click signals:** the analog pull and the click boolean are delivered on separate actions and must be shaped separately.
- **Hardware detent:** the runtime's own trigger detent asserts at roughly 45-50 percent of pull. With `click_mode=replace` and `click_compare=rawpull`, `trigger_click_threshold` acts as a fraction of trigger pull and replaces that detent.
- **Exponent saturation:** `trigger_curve_exponent` values above about 3 saturate the analog axis within a few percent of travel. With exponent 20 the game sees 0.87 at 40 percent of pull, which fires any axis-thresholded action almost immediately.
- **`trigger_range_max` had no in-game effect:** it was tested at 0.30, 0.10 and -1.10, and none of these values changed in-game behavior. The game appears to fire from the click boolean rather than the analog pull value, so range scaling does not affect firing in this title.
- **Hand animation froze:** when the trigger click boolean was overridden, the hand animation froze.

### Pose limitation

The grip-to-aim transform and the physical mount rotation are only observable as their product. No runtime-only capture can separate them, so offsets are tuned as a combined result.

## Logging

- Every action-state query is logged by action name.
- An `ACTION name=... sources=[...] hands=[...]` table is written at startup and can be dumped from the GUI. It is saved next to the log file, and its full path is logged as `ACTION_TABLE path=...`.
- `CLICK_DIAG` lines report click handling.
- `BOOLDIG` logs each boolean query. `ACTION` lines show the type, bound paths (suggested paths are marked) and role (axis, click, touch or other).
- `CONFIG trigger_range_max=... raw=... sanitized=...` is logged at startup.

## Repository layout

- `src/main.c`: legacy proxy configuration, logging, GUI integration, pose hooks and trigger setup.
- `src/openxr_input_layer.c`: OpenXR API-layer implementation. Use one of the two builds at a time, not both.
- `src/xr_core.c`: shared trigger normalization, click synthesis and pose core.
- `src/gui.c`, `src/gui.h`: Win32 popup GUI for pose offsets, capture and trigger controls.
- `tools/trigger_curve_test.c`: offline regression tests for the shared trigger core.
