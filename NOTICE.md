# NOTICE

## Fork status

This repository is a fork of [aruametello/openxr_loader-dll-proxy](https://github.com/aruametello/openxr_loader-dll-proxy).
Fork link: https://github.com/Trexpool/openxr_ACEXR_loader-dll-proxy

Upstream project URL: https://github.com/aruametello/openxr_loader-dll-proxy

## Licensing status

- The upstream project has no LICENSE file and no license statement.
- The upstream code therefore remains the property of its author. No permission to redistribute it has been granted as of this notice.
- No license is granted by this repository for any upstream code, and none is assumed.
- The upstream author has been contacted about licensing. This notice will be updated with the outcome.

## File origin

Files are listed against the upstream file list. "Derived" means the file began as upstream code and has been modified in this repository. "Original" means the file was written for this project and has no upstream counterpart.

| File | Origin | Notes |
|---|---|---|
| `src/Quaternion.c` | Derived | Upstream file. Whether it was modified has not been verified. |
| `src/Quaternion.h` | Derived | Upstream file. |
| `src/gui.c` | Derived | Upstream file, substantially rewritten: popup layout, shared font, trigger controls, calibration controls. |
| `src/gui.h` | Derived | Upstream file, extended. |
| `src/main.c` | Derived | Upstream file, substantially modified: configuration parsing, trigger processing, logging, GUI integration. |
| `src/main.h` | Derived | Upstream file, extended. |
| `src/openxr_layer_demo.c` | Derived | Upstream file. |
| `src/openxr_loader.def` | Derived | Upstream file. |
| `Makefile` | Derived | Upstream file, extended with OpenXR SDK path variables and new targets. |
| `README.md` | Derived | Upstream file, rewritten in this repository. |
| `openxr_loader_config.txt` | Derived | Upstream file, extended with new keys. |
| `openxr_api_layer_demo.json` | Derived | Upstream file. |
| `src/xr_core.c` | Original | Shared trigger and pose core added in this repository. |
| `src/xr_core.h` | Original | Header for the shared core. |
| `src/openxr_input_layer.c` | Original | OpenXR API-layer implementation added in this repository. |
| `src/_trigger_part.c` | Original | Trigger processing code added in this repository. |
| `tools/trigger_curve_test.c` | Original | Offline regression test for the shared core. |
| `tools/calib_analysis.c` | Original | Offline calibration analysis tool. |
| `CHANGELOG.md` | Original | Release history for this repository. |
| `OPENXR_API_LAYER.md` | Original | API-layer documentation. |
| `NOTICE.md` | Original | This file. |

Origin is based on the upstream file list supplied by the project author and on git history. Files not listed above, including scripts, sample configuration variants, and tools, should be reviewed before publication.

## Function-level status

No function-level comparison against upstream has been performed. No function is claimed to be unchanged. Classification in this file is at file level only, as shown in the table above.
