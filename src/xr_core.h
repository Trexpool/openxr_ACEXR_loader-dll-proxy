#pragma once
#include <openxr/openxr.h>

/* Shared by the legacy proxy (main.c) and the API layer (openxr_input_layer.c).
   Behavior here is the single source of truth; do not reimplement in callers. */

/* threshold >= 0: values below threshold -> 0 (cutoff).
   threshold <  0: gain of (1 + -threshold), clamped to [0,1]. */
float core_trigger_sensitivity(float original, float threshold);

/* Right-hand offset. rot_xyz are radians, stored as config keys x,y,z
   (rotation about OpenXR X, Y, Z respectively). pos_xyz are meters.
   frame_local=0: orientation = rotation * hand (world frame)
   frame_local=1: orientation = hand * rotation (local frame) */
void core_apply_right_offset(XrPosef *pose, const double rot_xyz[3],
							 const double pos_xyz[3], int frame_local);
