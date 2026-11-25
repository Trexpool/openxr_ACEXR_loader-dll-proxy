# Changelog

## Unreleased

### Changed
- Trigger `range_max` is sanitized in the shared core: any value in (0, 1] is accepted as-is; values outside that range (including 0, negative values and values above 1.0) fall back to 1.0.
- The `range_max` parsers in the legacy proxy (`src/main.c`) and the API layer (`src/openxr_input_layer.c`) now log the raw parsed value and a warning when sanitization changes it.
- Offline regression tests (`tools/trigger_curve_test.c`) cover `range_max` = 0.30, bit-identical output at 1.0, and invalid values 0.0, -1.10 and 1.5.

### Known limitations
- Changing `trigger_range_max` (tested at 0.30, 0.10 and -1.10) did not change behavior in Ready or Not VRO. The game appears to fire from the click boolean rather than the analog pull value.
- The popup GUI layout is not yet DPI-aware.
