#include "xr_core.h"
#include "Quaternion.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float clampf(float v, float lo, float hi)
{
  if (!(v >= lo)) /* also catches NaN */
	return lo;
  if (v == 0.0f)
	return 0.0f; /* never return -0.0f */
  if (v > hi)
	return hi;
  return v;
}

void core_trigger_cfg_defaults(CoreTriggerCfg *cfg)
{
  cfg->deadzone = 0.0f;
  cfg->exponent = 1.0f;
  cfg->press_threshold = 0.5f;
  cfg->gain = 1.0f;
}

void core_trigger_cfg_sanitize(CoreTriggerCfg *cfg)
{
  cfg->deadzone = clampf(cfg->deadzone, 0.0f, 0.9f);
  cfg->exponent = clampf(cfg->exponent, 0.01f, 20.0f);
  cfg->press_threshold = clampf(cfg->press_threshold, 0.0f, 1.0f);
  cfg->gain = clampf(cfg->gain, 0.0f, 100.0f);
}

float core_trigger_curve(float v, const CoreTriggerCfg *cfg)
{
  float v1 = clampf(v, 0.0f, 1.0f);
  if (cfg->deadzone > 0.0f)
	v1 = clampf((v1 - cfg->deadzone) / (1.0f - cfg->deadzone), 0.0f, 1.0f);
  if (cfg->exponent == 1.0f)
	return v1;
  return clampf(powf(v1, 1.0f / cfg->exponent), 0.0f, 1.0f);
}

float core_trigger_output(float v, const CoreTriggerCfg *cfg)
{
  return clampf(core_trigger_curve(v, cfg) * cfg->gain, 0.0f, 1.0f);
}

int core_trigger_pressed(float raw, const CoreTriggerCfg *cfg)
{
  return core_trigger_curve(raw, cfg) >= cfg->press_threshold;
}

int core_trigger_note_negative_threshold(void)
{
  static int noted;
  if (noted)
	return 0;
  noted = 1;
  return 1;
}

#define CORE_TRIGGER_TABLE 128

typedef struct
{
  XrAction handle;
  XrActionType type;
  int flags;
  char source[XR_MAX_PATH_LENGTH];
} TriggerSource;

static TriggerSource trigger_table[CORE_TRIGGER_TABLE];
static int trigger_table_count;
static int trigger_any_source;

static TriggerSource *trigger_entry(XrAction action, int create)
{
  for (int i = 0; i < trigger_table_count; ++i)
	if (trigger_table[i].handle == action)
	  return &trigger_table[i];
  if (!create || trigger_table_count >= CORE_TRIGGER_TABLE)
	return NULL;
  memset(&trigger_table[trigger_table_count], 0, sizeof(TriggerSource));
  trigger_table[trigger_table_count].handle = action;
  return &trigger_table[trigger_table_count++];
}

void core_trigger_reset_sources(void)
{
  trigger_table_count = 0;
  trigger_any_source = 0;
}

void core_trigger_note_action(XrAction action, XrActionType type)
{
  TriggerSource *e = trigger_entry(action, 1);
  if (e)
	e->type = type;
}

void core_trigger_mark_source(XrAction action, const char *path)
{
  static const char suffix[] = "/input/trigger/value";
  size_t len;
  size_t suffix_len = sizeof(suffix) - 1;
  int flag = 0;
  TriggerSource *e;

  if (!path)
	return;
  len = strlen(path);
  if (len <= suffix_len || strcmp(path + len - suffix_len, suffix) != 0)
	return;
  if (strncmp(path, "/user/hand/right/", 17) == 0)
	flag = CORE_HAND_RIGHT;
  else if (strncmp(path, "/user/hand/left/", 16) == 0)
	flag = CORE_HAND_LEFT;
  if (!flag)
	return;
  e = trigger_entry(action, 1);
  if (!e)
	return;
  e->flags |= flag;
  if (flag == CORE_HAND_RIGHT || !e->source[0])
	snprintf(e->source, sizeof(e->source), "%s", path);
  trigger_any_source = 1;
}

int core_trigger_hand_flags(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return e ? e->flags : 0;
}

const char *core_trigger_source_path(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return (e && e->source[0]) ? e->source : "<unknown>";
}

int core_trigger_sources_known(void)
{
  return trigger_any_source;
}

XrAction core_trigger_find_float_action(int hand_flag)
{
  for (int i = 0; i < trigger_table_count; ++i)
	if ((trigger_table[i].flags & hand_flag) && trigger_table[i].type == XR_ACTION_TYPE_FLOAT_INPUT)
	  return trigger_table[i].handle;
  return XR_NULL_HANDLE;
}

int core_trigger_method_first_use(int by_source_path)
{
  static int seen_source;
  static int seen_name;
  int *seen = by_source_path ? &seen_source : &seen_name;
  if (*seen)
	return 0;
  *seen = 1;
  return 1;
}

static float stat_min = 1e30f, stat_max = -1e30f;
static float stat_prev_out;
static unsigned long stat_edges;

void core_trigger_track(float raw, float out)
{
  unsigned int a, b;
  float prev = stat_prev_out;
  if (raw < stat_min)
	stat_min = raw;
  if (raw > stat_max)
	stat_max = raw;
  memcpy(&a, &prev, sizeof(a));
  memcpy(&b, &out, sizeof(b));
  if (a == 0u && b != 0u)
	++stat_edges;
  stat_prev_out = out;
}

int core_trigger_log_gate(float raw, float curved, int pressed, unsigned long long now_ms)
{
  static float last_raw = -1.0f;
  static float last_curved = -1.0f;
  static int last_pressed = -1;
  static unsigned long long last_ms;
  int changed = fabsf(raw - last_raw) > 0.01f || fabsf(curved - last_curved) > 0.01f ||
				pressed != last_pressed;
  if (!changed && !(now_ms - last_ms >= 1000ULL))
	return 0;
  if (last_pressed != -1 && now_ms - last_ms < 250ULL)
	return 0;
  last_raw = raw;
  last_curved = curved;
  last_pressed = pressed;
  last_ms = now_ms;
  return 1;
}

void core_trigger_format_diag(char *buf, size_t size, const char *method, const char *action_name,
							  XrAction action, const char *hand, float raw, float curved,
							  float out, int pressed, const CoreTriggerCfg *cfg)
{
  snprintf(buf, size,
		   "TRIGGER_DIAG method=%s action=%s hand=%s source=%s raw=%.3f curved=%.3f out=%.3f bool=%d "
		   "raw_min=%.4f raw_max=%.4f rising_edges=%lu "
			   "cfg{deadzone=%.3f exponent=%.3f gain=%.3f press_threshold=%.3f}",
			   method, action_name ? action_name : "<none>", hand, core_trigger_source_path(action), raw,
			   curved, out, pressed, stat_min, stat_max, stat_edges, cfg->deadzone, cfg->exponent, cfg->gain,
			   cfg->press_threshold);
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
