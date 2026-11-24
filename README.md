# openxr_loader.dll proxy

![screenshot](https://github.com/aruametello/openxr_loader-dll-proxy/blob/main/screenshot-2.png?raw=true)

A proxy dll to Khronos openxr_loader.dll. (https://registry.khronos.org/OpenXR/specs/1.0/loader.html)

video description and demonstration here: https://www.youtube.com/watch?v=WixaeDdFIWg

it exposes bilateral comunications between the XR aplication and the OpenXR api, allowing the user to change inputs and outputs to OpenXR.

the target here is to implement a setting (per game) of controller rotation and position offsets. 
I personally think every vr game should have this as an option the same way desktop mouse driven first person shooters have mouse sensitivity settings.


https://github.com/aruametello/openxr_loader-dll-proxy/assets/25987323/0478a50b-3348-48ee-9340-1d7ea83c6feb



in short: "i will do my own settings, with blackjack and hookers".

This could serve as a starting point for a more complex tool to tweak everything between OpenXR and aplication, like OpenVR Advanced Settings does for OpenVR.

ideas that i desire to explore in the future:
 * filter inputs (remove shaky hands) under certains scenarios.
    - tiny input to the trigger finger would enable the filter.
    
 * OpenXR does have "full hand poses" support, could we use those for anything interesting? (perhalps injecting an external data source or using REALLY custom hand posess)
 


# Compiling:

the precompiled binaries were made using msys2 mingw64-ucrt, but is likely that other compilers will work as well.

the makefile has a target named "dll_release" that produces the desired, required dll file for the proxy.
## Trigger settings

- `trigger_press_threshold` is a Boolean press threshold (0-1). Some games fire from the analog axis instead; for those, use `trigger_deadzone` and `trigger_curve_exponent`.
- A threshold of 0.0 makes the Boolean read as permanently pressed (curved value >= 0 is always true).
- `trigger_gain` affects only the analog value, which is always clamped to [0,1] after gain.
- Below `trigger_deadzone` the analog output is exactly 0.0.


## Fire-path instruments (trigger)
Config keys (proxy config and API-layer config):
- trigger_click_threshold (-1 = passthrough hardware /input/trigger/click, 0-1 = synthesize click = curved analog >= t). Most likely lever on fire depth. GUI: "Trigger click threshold: -1 = use hardware click, 0-1 = synthesize".
- trigger_force_value (-1 = disabled, 0-1 = deliver this constant as the trigger analog; log says forced). Bisect the game's threshold with it.
- trigger_range_max ((0,1], default 1): raw is divided by it before deadzone/exponent.
- trigger_target_action (default empty): with 2+ actions bound to trigger/value, curve only the named action.
- Every action-state query is logged by action NAME; ACTION name=.. sources=[..] hands=[..] table is dumped at startup and via the GUI button. CLICK_DIAG lines report click handling; unmodified trigger-bound queries are flagged.
- Exit summary: sample count, min, max, p99 of raw trigger.

Hypothesis (unproven): with the old build and trigger_press_threshold=0.0 the Boolean was (value >= 0.0), always true, which may explain the earlier no-fire results.

## Click matching, modes and comparison
- The click action is identified by its COMPLETE bound path: /user/hand/{left,right}/input/trigger (UE4 legacy Oculus boolean) or .../input/trigger/click. Never .../trigger/value (axis) or .../trigger/touch. Runtime-confirmed sources (xrEnumerateBoundSourcesForAction) win over suggested bindings.
- click_mode: replace (default) out = hit; or: out = runtime_click || hit; and: out = runtime_click && hit. Only active when trigger_click_threshold >= 0.
- click_compare: rawpull (default) compares the value after deadzone and range normalization, before exponent and gain, so the threshold is a fraction of trigger pull independent of the animation curve; curved compares the curved, gained value.
- BOOLDIG logs every boolean query; ACTION lines show type, bound paths (suggested ones marked) and role (axis/click/touch/other).

## Findings from in-game logs (Ready or Not VRO, UE4)
- In Ready or Not VRO the game creates a trigger CLICK action bound to /user/hand/{hand}/input/trigger - the UE4 legacy Oculus path, NOT the OpenXR standard /input/trigger/click. Any tool matching only the spec path silently does nothing on every UE4 title.
- UE4 creates FOUR axis actions (oculus, mixedreality, valveindex, vive) on the same source path. Only the one matching the active interaction profile is bound by the runtime; the others return 0.0. A tool that caches a per-hand analog value must only be updated by the BOUND action, or the cache flips between the real value and zero every frame and destroys press-edge detection. Log evidence: `analog_cached=0.000022 analog_rawpull_cached=0.584314`. The proxy now logs CACHEWRITE when the writer changes and INFO ANALOG-ROLE ACTION NOT BOUND BY RUNTIME for the unbound actions.
- The analog trigger axis and the trigger click boolean are SEPARATE signals. Ready or Not binds Fire to Gamepad_RightTrigger (derived from the axis) and exposes RightTriggerClick separately. Which one the game fires from must be determined per game. The FIREPATH line (once per second) shows both paths.
- The runtime's own trigger detent in Virtual Desktop asserts at roughly 45-50 percent of pull. With click_mode=replace and click_compare=rawpull, trigger_click_threshold is a FRACTION OF TRIGGER PULL and replaces that detent.
- trigger_curve_exponent values above about 3 saturate the analog axis within a few percent of travel. With exponent 20 the game sees 0.87 at 40 percent of pull, which fires any axis-thresholded action almost immediately. Use 1.0 to 2.0 for normal shaping.
- action_table.txt is written next to the log file, with a copy next to the config file; the full path is logged at startup as `ACTION_TABLE path=<full path>`.
