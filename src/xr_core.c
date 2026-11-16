#include "xr_core.h"
#include "Quaternion.h"

float core_trigger_sensitivity(float original, float threshold)
{
  float adjusted;
  if (threshold >= 0.0f)
	return original < threshold ? 0.0f : original;

  adjusted = original * (1.0f + (-threshold));
  if (adjusted > 1.0f)
	adjusted = 1.0f;
  if (adjusted < 0.0f)
	adjusted = 0.0f;
  return adjusted;
}

void core_apply_right_offset(XrPosef *pose, const double rot_xyz[3],
							 const double pos_xyz[3], int frame_local)
{
  Quaternion hand, original_hand, rotation;
  float rotation_zyx[3];
  float offset[3];
  float rotated[3];

  /* Library stores zyxw, OpenXR is xyzw. */
  hand.v[Z_AXIS] = pose->orientation.z;
  hand.v[Y_AXIS] = pose->orientation.y;
  hand.v[X_AXIS] = pose->orientation.x;
  hand.w = pose->orientation.w;
  original_hand = hand;

  rotation_zyx[Z_AXIS] = (float)rot_xyz[2];
  rotation_zyx[Y_AXIS] = (float)rot_xyz[1];
  rotation_zyx[X_AXIS] = (float)rot_xyz[0];
  Quaternion_fromEulerZYX(rotation_zyx, &rotation);

  if (frame_local)
	Quaternion_multiply(&hand, &rotation, &hand);
  else
	Quaternion_multiply(&rotation, &hand, &hand);

  offset[0] = (float)pos_xyz[0];
  offset[1] = (float)pos_xyz[1];
  offset[2] = (float)pos_xyz[2];
  if (frame_local)
	Quaternion_rotate(&original_hand, offset, rotated);
  else
	Quaternion_rotate(&rotation, offset, rotated);

  pose->position.x += rotated[0];
  pose->position.y += rotated[1];
  pose->position.z += rotated[2];

  pose->orientation.z = hand.v[Z_AXIS];
  pose->orientation.y = hand.v[Y_AXIS];
  pose->orientation.x = hand.v[X_AXIS];
  pose->orientation.w = hand.w;
}
