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
  cfg->force_value = -1.0f;
  cfg->range_max = 1.0f;
  cfg->click_threshold = -1.0f;
}

void core_trigger_cfg_sanitize(CoreTriggerCfg *cfg)
{
  cfg->deadzone = clampf(cfg->deadzone, 0.0f, 0.9f);
  cfg->exponent = clampf(cfg->exponent, 0.01f, 20.0f);
  cfg->press_threshold = clampf(cfg->press_threshold, 0.0f, 1.0f);
  cfg->gain = clampf(cfg->gain, 0.0f, 100.0f);
  cfg->force_value = (cfg->force_value >= 0.0f) ? clampf(cfg->force_value, 0.0f, 1.0f) : -1.0f;
  if (!(cfg->range_max > 0.0f) || cfg->range_max > 1.0f)
	cfg->range_max = 1.0f;
  else if (cfg->range_max < 0.05f)
	cfg->range_max = 0.05f;
  cfg->click_threshold = (cfg->click_threshold >= 0.0f) ? clampf(cfg->click_threshold, 0.0f, 1.0f) : -1.0f;
}

float core_trigger_curve(float v, const CoreTriggerCfg *cfg)
{
  float v1;
  if (cfg->force_value >= 0.0f)
	return clampf(cfg->force_value, 0.0f, 1.0f);
  v1 = v;
  if (cfg->range_max < 1.0f)
	v1 = v / cfg->range_max;
  v1 = clampf(v1, 0.0f, 1.0f);
  if (cfg->deadzone > 0.0f)
	v1 = clampf((v1 - cfg->deadzone) / (1.0f - cfg->deadzone), 0.0f, 1.0f);
  if (cfg->exponent == 1.0f)
	return v1;
  return clampf(powf(v1, 1.0f / cfg->exponent), 0.0f, 1.0f);
}

float core_trigger_output(float v, const CoreTriggerCfg *cfg)
{
  if (cfg->force_value >= 0.0f)
	return clampf(cfg->force_value, 0.0f, 1.0f);
  return clampf(core_trigger_curve(v, cfg) * cfg->gain, 0.0f, 1.0f);
}

int core_trigger_pressed(float raw, const CoreTriggerCfg *cfg)
{
  return core_trigger_curve(raw, cfg) >= cfg->press_threshold;
}

int core_trigger_click_enabled(const CoreTriggerCfg *cfg)
{
  return cfg->click_threshold >= 0.0f;
}

int core_trigger_click(float raw, const CoreTriggerCfg *cfg)
{
  return core_trigger_output(raw, cfg) >= cfg->click_threshold;
}

int core_trigger_note_negative_threshold(void)
{
  static int noted;
  if (noted)
	return 0;
  noted = 1;
  return 1;
}

#define CORE_TRIGGER_TABLE 512
#define CORE_SRC_MAX 8

typedef struct
{
  XrAction handle;
  XrActionType type;
  int flags;       /* bound to .../input/trigger/value, per hand */
  int click_flags; /* bound to .../input/trigger/click, per hand */
  int hands;       /* any bound source, per hand */
  char name[XR_MAX_ACTION_NAME_SIZE];
  char source[XR_MAX_PATH_LENGTH];
  char click_source[XR_MAX_PATH_LENGTH];
  int nsrc;
  char src[CORE_SRC_MAX][XR_MAX_PATH_LENGTH];
  int seen_click_query;
  int seen_unmodified_query;
} TriggerSource;

static TriggerSource trigger_table[CORE_TRIGGER_TABLE];
static int trigger_table_count;
static int trigger_any_source;
static char trigger_target[XR_MAX_ACTION_NAME_SIZE];

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

void core_trigger_note_action(XrAction action, XrActionType type, const char *name)
{
  TriggerSource *e = trigger_entry(action, 1);
  if (!e)
	return;
  e->type = type;
  if (name)
	snprintf(e->name, sizeof(e->name), "%s", name);
}

void core_trigger_mark_source(XrAction action, const char *path)
{
  static const char suffix_value[] = "/input/trigger/value";
  static const char suffix_click[] = "/input/trigger/click";
  size_t len;
  int flag = 0;
  TriggerSource *e;

  if (!path || !path[0])
	return;
  e = trigger_entry(action, 1);
  if (!e)
	return;
  len = strlen(path);

  if (strncmp(path, "/user/hand/right/", 17) == 0)
	flag = CORE_HAND_RIGHT;
  else if (strncmp(path, "/user/hand/left/", 16) == 0)
	flag = CORE_HAND_LEFT;
  e->hands |= flag;

  {
	int known = 0;
	for (int i = 0; i < e->nsrc; ++i)
	  if (strcmp(e->src[i], path) == 0)
		known = 1;
	if (!known && e->nsrc < CORE_SRC_MAX)
	  snprintf(e->src[e->nsrc++], XR_MAX_PATH_LENGTH, "%s", path);
  }

  if (!flag)
	return;
  if (len > sizeof(suffix_value) - 1 && strcmp(path + len - (sizeof(suffix_value) - 1), suffix_value) == 0)
  {
	e->flags |= flag;
	if (flag == CORE_HAND_RIGHT || !e->source[0])
	  snprintf(e->source, sizeof(e->source), "%s", path);
	trigger_any_source = 1;
  }
  else if (len > sizeof(suffix_click) - 1 && strcmp(path + len - (sizeof(suffix_click) - 1), suffix_click) == 0)
  {
	e->click_flags |= flag;
	if (flag == CORE_HAND_RIGHT || !e->click_source[0])
	  snprintf(e->click_source, sizeof(e->click_source), "%s", path);
  }
}

int core_trigger_hand_flags(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return e ? e->flags : 0;
}

int core_trigger_click_flags(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return e ? e->click_flags : 0;
}

const char *core_trigger_source_path(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return (e && e->source[0]) ? e->source : "<no-trigger-value-source>";
}

const char *core_click_source_path(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return (e && e->click_source[0]) ? e->click_source : "<no-trigger-click-source>";
}

int core_trigger_sources_known(void)
{
  return trigger_any_source;
}

const char *core_action_name(XrAction action)
{
  static char fallback[64];
  TriggerSource *e = trigger_entry(action, 0);
  if (e && e->name[0])
	return e->name;
  snprintf(fallback, sizeof(fallback), "<uncaptured:handle=%llu>", (unsigned long long)action);
  return fallback;
}

void core_trigger_set_target(const char *name)
{
  snprintf(trigger_target, sizeof(trigger_target), "%s", name ? name : "");
}

const char *core_trigger_target(void)
{
  return trigger_target;
}

int core_trigger_value_action_count(int hand_flag)
{
  int n = 0;
  for (int i = 0; i < trigger_table_count; ++i)
	if (trigger_table[i].flags & hand_flag)
	  ++n;
  return n;
}

int core_trigger_target_excludes(XrAction action)
{
  if (!trigger_target[0] || core_trigger_value_action_count(CORE_HAND_RIGHT) < 2)
	return 0;
  return _stricmp(core_action_name(action), trigger_target) != 0;
}

XrAction core_trigger_find_float_action(int hand_flag)
{
  int use_target = trigger_target[0] && core_trigger_value_action_count(hand_flag) >= 2;
  for (int i = 0; i < trigger_table_count; ++i)
	if ((trigger_table[i].flags & hand_flag) && trigger_table[i].type == XR_ACTION_TYPE_FLOAT_INPUT &&
		(!use_target || _stricmp(trigger_table[i].name, trigger_target) == 0))
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

int core_query_note(XrAction action, int modified)
{
  int r = 0;
  TriggerSource *e = trigger_entry(action, 0);
  if (!e)
	return 0;
  if (e->click_flags && !e->seen_click_query)
  {
	e->seen_click_query = 1;
	r |= CORE_QUERY_FIRST_CLICK;
  }
  if ((e->flags || e->click_flags) && !modified && !e->seen_unmodified_query)
  {
	e->seen_unmodified_query = 1;
	r |= CORE_QUERY_FIRST_UNMODIFIED;
  }
  return r;
}

int core_action_table_count(void)
{
  return trigger_table_count;
}

int core_action_table_format(int index, char *buf, size_t size)
{
  char sources[CORE_SRC_MAX * (XR_MAX_PATH_LENGTH + 1) + 8];
  char hands[8];
  const TriggerSource *e;
  size_t used = 0;

  if (index < 0 || index >= trigger_table_count)
	return 0;
  e = &trigger_table[index];
  sources[0] = 0;
  for (int i = 0; i < e->nsrc; ++i)
	used += (size_t)snprintf(sources + used, sizeof(sources) - used, "%s%s", i ? " " : "", e->src[i]);
  snprintf(hands, sizeof(hands), "%s%s%s", (e->hands & CORE_HAND_LEFT) ? "l" : "",
		   ((e->hands & CORE_HAND_LEFT) && (e->hands & CORE_HAND_RIGHT)) ? " " : "",
		   (e->hands & CORE_HAND_RIGHT) ? "r" : "");
  snprintf(buf, size, "ACTION name=\"%s\" handle=%llu sources=[%s] hands=[%s]", core_action_name(e->handle),
		   (unsigned long long)e->handle, sources, hands);
  return 1;
}

static float stat_min, stat_max;
static float stat_prev_out;
static unsigned long stat_edges;
static unsigned long stat_count;
static unsigned long stat_hist[1001];

void core_trigger_track(float raw, float out)
{
  unsigned int a, b;
  float prev = stat_prev_out;
  float h = clampf(raw, 0.0f, 1.0f);
  if (stat_count == 0 || raw < stat_min)
	stat_min = raw;
  if (stat_count == 0 || raw > stat_max)
	stat_max = raw;
  stat_hist[(int)(h * 1000.0f + 0.5f)]++;
  stat_count++;
  memcpy(&a, &prev, sizeof(a));
  memcpy(&b, &out, sizeof(b));
  if (a == 0u && b != 0u)
	++stat_edges;
  stat_prev_out = out;
}

void core_trigger_summary(char *buf, size_t size)
{
  unsigned long target = (stat_count * 99UL + 99UL) / 100UL;
  unsigned long acc = 0;
  int bin = 0;
  if (target < 1)
	target = 1;
  for (; bin < 1001; ++bin)
  {
	acc += stat_hist[bin];
	if (acc >= target)
	  break;
  }
  if (bin > 1000)
	bin = 1000;
  snprintf(buf, size, "TRIGGER_SUMMARY samples=%lu raw_min=%.6f raw_max=%.6f raw_p99=%.6f rising_edges=%lu",
		   stat_count, stat_count ? stat_min : 0.0f, stat_count ? stat_max : 0.0f,
		   stat_count ? bin / 1000.0f : 0.0f, stat_edges);
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

int core_click_log_gate(int runtime, int out, unsigned long long now_ms)
{
  static int last_runtime = -1;
  static int last_out = -1;
  static unsigned long long last_ms;
  int changed = runtime != last_runtime || out != last_out;
  if (!changed && !(now_ms - last_ms >= 1000ULL))
	return 0;
  if (last_runtime != -1 && now_ms - last_ms < 250ULL)
	return 0;
  last_runtime = runtime;
  last_out = out;
  last_ms = now_ms;
  return 1;
}

void core_trigger_format_diag(char *buf, size_t size, const char *method, const char *action_name,
							  XrAction action, const char *hand, float raw, float curved,
							  float out, int pressed, const CoreTriggerCfg *cfg)
{
  char mode[48];
  if (cfg->force_value >= 0.0f)
	snprintf(mode, sizeof(mode), "forced(%.6f)", cfg->force_value);
  else
	snprintf(mode, sizeof(mode), "live");
  snprintf(buf, size,
		   "TRIGGER_DIAG mode=%s method=%s action=%s hand=%s source=%s raw=%.6f curved=%.6f out=%.6f bool=%d "
		   "raw_min=%.6f raw_max=%.6f rising_edges=%lu "
		   "cfg{deadzone=%.3f exponent=%.3f gain=%.3f press_threshold=%.3f range_max=%.3f click_threshold=%.3f}",
		   mode, method, action_name ? action_name : core_action_name(action), hand,
		   core_trigger_source_path(action), raw, curved, out, pressed,
		   stat_count ? stat_min : 0.0f, stat_count ? stat_max : 0.0f, stat_edges, cfg->deadzone,
		   cfg->exponent, cfg->gain, cfg->press_threshold, cfg->range_max, cfg->click_threshold);
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
