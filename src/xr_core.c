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
  cfg->click_mode = CORE_CLICK_REPLACE;
  cfg->click_compare = CORE_COMPARE_RAWPULL;
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
  if (cfg->click_mode < CORE_CLICK_REPLACE || cfg->click_mode > CORE_CLICK_AND)
    cfg->click_mode = CORE_CLICK_REPLACE;
  if (cfg->click_compare != CORE_COMPARE_CURVED)
    cfg->click_compare = CORE_COMPARE_RAWPULL;
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

float core_trigger_pull(float v, const CoreTriggerCfg *cfg)
{
  float v1 = v;
  if (cfg->force_value >= 0.0f)
	return clampf(cfg->force_value, 0.0f, 1.0f);
  if (cfg->range_max < 1.0f)
	v1 = v / cfg->range_max;
  v1 = clampf(v1, 0.0f, 1.0f);
  if (cfg->deadzone > 0.0f)
	v1 = clampf((v1 - cfg->deadzone) / (1.0f - cfg->deadzone), 0.0f, 1.0f);
  return v1;
}

float core_trigger_click_compare(float raw, const CoreTriggerCfg *cfg)
{
  if (cfg->click_compare == CORE_COMPARE_CURVED)
	return core_trigger_output(raw, cfg);
  return core_trigger_pull(raw, cfg);
}

int core_trigger_click_value(float compare, int runtime_click, const CoreTriggerCfg *cfg)
{
  int hit;
  runtime_click = runtime_click ? 1 : 0;
  if (!core_trigger_click_enabled(cfg))
	return runtime_click;
  hit = compare >= cfg->click_threshold;
  if (cfg->click_mode == CORE_CLICK_OR)
	return runtime_click || hit;
  if (cfg->click_mode == CORE_CLICK_AND)
	return runtime_click && hit;
  return hit;
}

int core_trigger_click(float raw, int runtime_click, const CoreTriggerCfg *cfg)
{
  return core_trigger_click_value(core_trigger_click_compare(raw, cfg), runtime_click, cfg);
}

int core_click_mode_parse(const char *s)
{
  if (!s)
	return -1;
  if (strcmp(s, "replace") == 0)
	return CORE_CLICK_REPLACE;
  if (strcmp(s, "or") == 0)
	return CORE_CLICK_OR;
  if (strcmp(s, "and") == 0)
	return CORE_CLICK_AND;
  return -1;
}

const char *core_click_mode_name(int mode)
{
  return mode == CORE_CLICK_OR ? "or" : (mode == CORE_CLICK_AND ? "and" : "replace");
}

int core_click_compare_parse(const char *s)
{
  if (!s)
	return -1;
  if (strcmp(s, "rawpull") == 0)
	return CORE_COMPARE_RAWPULL;
  if (strcmp(s, "curved") == 0)
	return CORE_COMPARE_CURVED;
  return -1;
}

const char *core_click_compare_name(int compare)
{
  return compare == CORE_COMPARE_CURVED ? "curved" : "rawpull";
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
  int hands; /* any bound source, per hand */
  char name[XR_MAX_ACTION_NAME_SIZE];
  int nsrc;
  char src[CORE_SRC_MAX][XR_MAX_PATH_LENGTH];
  char rt[CORE_SRC_MAX]; /* 1 = confirmed by xrEnumerateBoundSourcesForAction */
  int has_rt;
  int seen_click_query;
  int seen_unmodified_query;
  int role_cached;
  int role;
  int role_hands;
  int role_method;
  int last_rt;
  int last_syn;
  int warned_noanalog;
  unsigned long queries;
  int rt_nonzero;
  int zero_noted;
} TriggerSource;

typedef struct
{
  float analog, raw, exponent, gain;
  int seen, forced, warned;
  XrAction writer;
  unsigned changes;
} HandCache;
static HandCache hand_cache[2];
static volatile long cache_lock;

static void cache_acquire(void)
{
  while (__sync_lock_test_and_set(&cache_lock, 1))
	;
}

static void cache_release(void)
{
  __sync_lock_release(&cache_lock);
}

static TriggerSource trigger_table[CORE_TRIGGER_TABLE];
static int trigger_table_count;
static int trigger_any_source;
static char trigger_target[XR_MAX_ACTION_NAME_SIZE];

typedef struct
{
  XrAction axis_action, click_action;
  float axis_out, compare;
  int click_out, runtime_click;
} FireState;
static FireState fire_state[2];

/* Classifies a COMPLETE source path. Never a loose suffix test. */
int core_trigger_classify_path(const char *path, int *hand_flag)
{
  const char *rest;
  int hand;
  if (hand_flag)
	*hand_flag = 0;
  if (!path)
	return CORE_ROLE_OTHER;
  if (strncmp(path, "/user/hand/right", 16) == 0 && path[16] == '/')
  {
	hand = CORE_HAND_RIGHT;
	rest = path + 16;
  }
  else if (strncmp(path, "/user/hand/left", 15) == 0 && path[15] == '/')
  {
	hand = CORE_HAND_LEFT;
	rest = path + 15;
  }
  else
	return CORE_ROLE_OTHER;
  if (hand_flag)
	*hand_flag = hand;
  if (strcmp(rest, "/input/trigger/value") == 0)
	return CORE_ROLE_AXIS;
  if (strcmp(rest, "/input/trigger") == 0 || strcmp(rest, "/input/trigger/click") == 0)
	return CORE_ROLE_CLICK;
  if (strcmp(rest, "/input/trigger/touch") == 0)
	return CORE_ROLE_TOUCH;
  return CORE_ROLE_OTHER;
}

int core_trigger_path_matches(const char *path, int role, int hand_flag)
{
  int hand = 0;
  int r = core_trigger_classify_path(path, &hand);
  return r == role && r != CORE_ROLE_OTHER && hand == hand_flag;
}

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

/* Runtime-confirmed sources win; suggested bindings are only the fallback. */
static int entry_uses(const TriggerSource *e, int i)
{
  return !e->has_rt || e->rt[i];
}

static int entry_flags(const TriggerSource *e, int role)
{
  int f = 0;
  for (int i = 0; i < e->nsrc; ++i)
  {
	int hand = 0;
	if (entry_uses(e, i) && core_trigger_classify_path(e->src[i], &hand) == role)
	  f |= hand;
  }
  return f;
}

static const char *entry_source(const TriggerSource *e, int role, int *confirmed)
{
  const char *found = NULL;
  int found_rt = 0;
  for (int i = 0; i < e->nsrc; ++i)
  {
	int hand = 0;
	if (entry_uses(e, i) && core_trigger_classify_path(e->src[i], &hand) == role)
	  if (!found || hand == CORE_HAND_RIGHT)
	  {
		found = e->src[i];
		found_rt = e->rt[i];
	  }
  }
  if (confirmed)
	*confirmed = found_rt;
  return found;
}

void core_trigger_reset_sources(void)
{
  trigger_table_count = 0;
  trigger_any_source = 0;
  memset(hand_cache, 0, sizeof(hand_cache));
  memset(fire_state, 0, sizeof(fire_state));
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

static void mark_source(XrAction action, const char *path, int confirmed)
{
  int hand = 0;
  int known = -1;
  TriggerSource *e;

  if (!path || !path[0])
	return;
  e = trigger_entry(action, 1);
  if (!e)
	return;
  core_trigger_classify_path(path, &hand);
  e->hands |= hand;
  for (int i = 0; i < e->nsrc; ++i)
	if (strcmp(e->src[i], path) == 0)
	  known = i;
  if (known < 0 && e->nsrc < CORE_SRC_MAX)
  {
	known = e->nsrc++;
	snprintf(e->src[known], XR_MAX_PATH_LENGTH, "%s", path);
	e->rt[known] = 0;
  }
  if (known >= 0 && confirmed)
  {
	e->rt[known] = 1;
	e->has_rt = 1;
  }
  if (entry_flags(e, CORE_ROLE_AXIS))
	trigger_any_source = 1;
}

void core_trigger_mark_source(XrAction action, const char *path)
{
  mark_source(action, path, 0);
}

void core_trigger_mark_source_confirmed(XrAction action, const char *path)
{
  mark_source(action, path, 1);
}

int core_trigger_hand_flags(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return e ? entry_flags(e, CORE_ROLE_AXIS) : 0;
}

int core_trigger_click_flags(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  return e ? entry_flags(e, CORE_ROLE_CLICK) : 0;
}

const char *core_trigger_source_path(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  const char *p = e ? entry_source(e, CORE_ROLE_AXIS, NULL) : NULL;
  return p ? p : "<none>";
}

const char *core_click_source_path(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  const char *p = e ? entry_source(e, CORE_ROLE_CLICK, NULL) : NULL;
  return p ? p : "<none>";
}

int core_click_source_confirmed(XrAction action)
{
  int confirmed = 0;
  TriggerSource *e = trigger_entry(action, 0);
  if (e)
	entry_source(e, CORE_ROLE_CLICK, &confirmed);
  return confirmed;
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
	if (entry_flags(&trigger_table[i], CORE_ROLE_AXIS) & hand_flag)
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
	if ((entry_flags(&trigger_table[i], CORE_ROLE_AXIS) & hand_flag) &&
		trigger_table[i].type == XR_ACTION_TYPE_FLOAT_INPUT &&
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

static int ci_has(const char *lower, const char *w)
{
  return strstr(lower, w) != NULL;
}

static void resolve_role(TriggerSource *e)
{
  if (e->role_cached)
	return;
  for (int pass = 0; pass < 2; ++pass)
  {
	int ah = 0, ch = 0;
	if (pass == 0 && !e->has_rt)
	  continue;
	for (int i = 0; i < e->nsrc; ++i)
	{
	  int hand = 0, r;
	  if (pass == 0 && !e->rt[i])
		continue;
	  r = core_trigger_classify_path(e->src[i], &hand);
	  if (r == CORE_ROLE_AXIS)
		ah |= hand;
	  else if (r == CORE_ROLE_CLICK)
		ch |= hand;
	}
	if (ah || ch)
	{
	  e->role = ah ? CORE_ROLE_AXIS : CORE_ROLE_CLICK;
	  e->role_hands = ah ? ah : ch;
	  e->role_method = pass == 0 ? CORE_METHOD_PATH : CORE_METHOD_SUGGESTED;
	  e->role_cached = 1;
	  return;
	}
  }
  if (e->nsrc == 0 && e->name[0])
  {
	char low[XR_MAX_ACTION_NAME_SIZE];
	int role = CORE_ROLE_OTHER;
	size_t n;
	for (n = 0; e->name[n] && n < sizeof(low) - 1; ++n)
	  low[n] = (e->name[n] >= 'A' && e->name[n] <= 'Z') ? (char)(e->name[n] + 32) : e->name[n];
	low[n] = 0;
	if (ci_has(low, "trigger") && (ci_has(low, "click") || ci_has(low, "pressed")))
	  role = CORE_ROLE_CLICK;
	else if (ci_has(low, "trigger") && (ci_has(low, "axis") || ci_has(low, "value") || ci_has(low, "pull") || strcmp(low, "trigger") == 0))
	  role = CORE_ROLE_AXIS;
	if (role != CORE_ROLE_OTHER)
	{
	  int h = 0;
	  if (ci_has(low, "left"))
		h = CORE_HAND_LEFT;
	  else if (ci_has(low, "right"))
		h = CORE_HAND_RIGHT;
	  else
		h = CORE_HAND_LEFT | CORE_HAND_RIGHT;
	  e->role = role;
	  e->role_hands = h;
	  e->role_method = CORE_METHOD_NAME;
	  e->role_cached = 1;
	}
  }
}

int core_action_role(XrAction action, int *hands, int *method)
{
  TriggerSource *e = trigger_entry(action, 0);
  if (hands)
	*hands = 0;
  if (method)
	*method = 0;
  if (!e)
	return CORE_ROLE_OTHER;
  resolve_role(e);
  if (!e->role_cached)
	return CORE_ROLE_OTHER;
  if (hands)
	*hands = e->role_hands;
  if (method)
	*method = e->role_method;
  return e->role;
}

const char *core_role_name(int role)
{
  return role == CORE_ROLE_AXIS ? "axis" : role == CORE_ROLE_CLICK ? "click" : "other";
}

const char *core_role_method_name(int method)
{
  return method == CORE_METHOD_PATH ? "path" : method == CORE_METHOD_SUGGESTED ? "suggested"
		 : method == CORE_METHOD_NAME ? "name" : "none";
}

static int hand_index(int hand)
{
  return hand == CORE_HAND_LEFT ? 1 : 0;
}

/* Both fields come from the SAME sample and are written together. */
static void cache_store(HandCache *h, float raw, const CoreTriggerCfg *cfg)
{
  float pull = core_trigger_pull(raw, cfg);
  float out = core_trigger_output(raw, cfg);
  h->raw = pull;
  h->analog = out;
  h->exponent = cfg->exponent;
  h->gain = cfg->gain;
  h->forced = cfg->force_value >= 0.0f;
  h->seen = 1;
}

void core_analog_store(int hand, float raw, const CoreTriggerCfg *cfg)
{
  cache_acquire();
  cache_store(&hand_cache[hand_index(hand)], raw, cfg);
  cache_release();
}

int core_action_bound(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  if (!e || !e->has_rt)
	return 0;
  for (int i = 0; i < e->nsrc; ++i)
  {
	int r = core_trigger_classify_path(e->src[i], NULL);
	if (e->rt[i] && (r == CORE_ROLE_AXIS || r == CORE_ROLE_CLICK))
	  return 1;
  }
  return 0;
}

static int hand_has_bound_axis(int hand)
{
  for (int i = 0; i < trigger_table_count; ++i)
  {
	TriggerSource *e = &trigger_table[i];
	resolve_role(e);
	if (e->role_cached && e->role == CORE_ROLE_AXIS && (e->role_hands & hand) && core_action_bound(e->handle))
	  return 1;
  }
  return 0;
}

/* Only the runtime-bound analog action may write the per-hand cache. */
int core_analog_write(int hand, XrAction action, float raw, const CoreTriggerCfg *cfg)
{
  HandCache *h = &hand_cache[hand_index(hand)];
  int result = CORE_WRITE_WRITTEN;
  int bound_hand = hand_has_bound_axis(hand);
  int bound_self = core_action_bound(action);
  cache_acquire();
  if (bound_hand ? !bound_self : (h->writer && h->writer != action))
  {
	cache_release();
	return 0;
  }
  if (!bound_hand && !h->writer && raw == 0.0f)
  {
	cache_store(h, raw, cfg);
	cache_release();
	return CORE_WRITE_WRITTEN;
  }
  if (h->writer != action)
  {
	h->writer = action;
	h->changes++;
	result |= CORE_WRITE_NEWWRITER;
  }
  cache_store(h, raw, cfg);
  if (!h->forced && !h->warned)
  {
	float expect = h->exponent == 1.0f ? h->raw : powf(h->raw, 1.0f / h->exponent);
	expect = clampf(expect * h->gain, 0.0f, 1.0f);
	if (fabsf(expect - h->analog) > 1e-4f)
	{
	  h->warned = 1;
	  result |= CORE_WRITE_INCONSISTENT;
	}
  }
  cache_release();
  return result;
}

XrAction core_analog_writer(int hand, unsigned *changes)
{
  HandCache *h = &hand_cache[hand_index(hand)];
  if (changes)
	*changes = h->changes;
  return h->writer;
}

int core_analog_cached(int hand, float *analog, float *rawpull)
{
  HandCache s;
  cache_acquire();
  s = hand_cache[hand_index(hand)];
  cache_release();
  if (analog)
	*analog = s.analog;
  if (rawpull)
	*rawpull = s.raw;
  return s.seen;
}

int core_click_synth(int hand, int runtime_click, const CoreTriggerCfg *cfg, float *compare, int *have_analog)
{
  HandCache s;
  float c = 0.0f;
  cache_acquire();
  s = hand_cache[hand_index(hand)];
  cache_release();
  runtime_click = runtime_click ? 1 : 0;
  if (s.seen)
	c = cfg->click_compare == CORE_COMPARE_CURVED ? s.analog : s.raw;
  if (compare)
	*compare = c;
  if (have_analog)
	*have_analog = s.seen;
  if (!s.seen || !core_trigger_click_enabled(cfg))
	return runtime_click;
  return core_trigger_click_value(c, runtime_click, cfg);
}

void core_fire_note_axis(int hand, XrAction action, float out)
{
  FireState *f = &fire_state[hand_index(hand)];
  f->axis_action = action;
  f->axis_out = out;
}

void core_fire_note_click(int hand, XrAction action, int runtime_click, int out, float compare)
{
  FireState *f = &fire_state[hand_index(hand)];
  f->click_action = action;
  f->runtime_click = runtime_click ? 1 : 0;
  f->click_out = out ? 1 : 0;
  f->compare = compare;
}

int core_fire_due(unsigned long long now_ms)
{
  static unsigned long long last;
  if (last && now_ms - last < 1000ULL)
	return 0;
  last = now_ms;
  return 1;
}

int core_fire_format(int hand, const CoreTriggerCfg *cfg, char *buf, size_t size)
{
  const FireState *f = &fire_state[hand_index(hand)];
  const HandCache *h = &hand_cache[hand_index(hand)];
  char axis[96], click[96], writer[96];
  if (!f->axis_action && !f->click_action)
	return 0;
  snprintf(axis, sizeof(axis), "%s", f->axis_action ? core_action_name(f->axis_action) : "NONE");
  snprintf(click, sizeof(click), "%s", f->click_action ? core_action_name(f->click_action) : "NONE");
  snprintf(writer, sizeof(writer), "%s", h->writer ? core_action_name(h->writer) : "NONE");
  snprintf(buf, size,
		   "FIREPATH hand=%s axis_action=\"%s\" axis_out=%.6f click_action=\"%s\" click_out=%d runtime_click=%d compare=%.6f threshold=%.6f mode=%s cache_writer=\"%s\"",
		   hand == CORE_HAND_LEFT ? "left" : "right", axis, f->axis_out, click, f->click_out, f->runtime_click,
		   f->compare, cfg->click_threshold, core_click_mode_name(cfg->click_mode), writer);
  return 1;
}

int core_click_warn_noanalog(XrAction action)
{
  TriggerSource *e = trigger_entry(action, 0);
  if (!e || e->warned_noanalog)
	return 0;
  e->warned_noanalog = 1;
  return 1;
}

int core_click_changed(XrAction action, int out)
{
  TriggerSource *e = trigger_entry(action, 0);
  out = out ? 1 : 0;
  if (!e || e->last_syn == out)
	return 0;
  e->last_syn = out;
  return 1;
}

int core_detent_note(XrAction action, int runtime_click)
{
  TriggerSource *e = trigger_entry(action, 0);
  runtime_click = runtime_click ? 1 : 0;
  if (!e || e->last_rt == runtime_click)
	return 0;
  e->last_rt = runtime_click;
  return runtime_click ? 1 : 2;
}

int core_pair_format(int hand, char *buf, size_t size)
{
  char an[1024], ck[1024];
  size_t ua = 0, uc = 0;
  an[0] = ck[0] = 0;
  for (int i = 0; i < trigger_table_count; ++i)
  {
	TriggerSource *e = &trigger_table[i];
	resolve_role(e);
	if (!e->role_cached || !(e->role_hands & hand))
	  continue;
	if (e->role == CORE_ROLE_AXIS && ua < sizeof(an))
	  ua += (size_t)snprintf(an + ua, sizeof(an) - ua, "%s\"%s\"", ua ? "," : "", core_action_name(e->handle));
	else if (e->role == CORE_ROLE_CLICK && uc < sizeof(ck))
	  uc += (size_t)snprintf(ck + uc, sizeof(ck) - uc, "%s\"%s\"", uc ? "," : "", core_action_name(e->handle));
  }
  snprintf(buf, size, "PAIR hand=%s analog_action=%s click_action=%s", hand == CORE_HAND_LEFT ? "left" : "right",
		   an[0] ? an : "NONE", ck[0] ? ck : "NONE");
  return 1;
}

int core_action_role_format(int index, char *buf, size_t size)
{
  TriggerSource *e;
  if (index < 0 || index >= trigger_table_count)
	return 0;
  e = &trigger_table[index];
  resolve_role(e);
  snprintf(buf, size, "ROLE action=\"%s\" handle=%llu role=%s method=%s", core_action_name(e->handle),
		   (unsigned long long)e->handle, core_role_name(e->role_cached ? e->role : CORE_ROLE_OTHER),
		   core_role_method_name(e->role_cached ? e->role_method : 0));
  return 1;
}

int core_query_note(XrAction action, int modified, int runtime_nonzero)
{
  int r = 0;
  int hands = 0, m = 0;
  int role;
  TriggerSource *e = trigger_entry(action, 0);
  if (!e)
	return 0;
  role = core_action_role(action, &hands, &m);
  if (role == CORE_ROLE_CLICK && !e->seen_click_query)
  {
	e->seen_click_query = 1;
	r |= CORE_QUERY_FIRST_CLICK;
  }
  if (role == CORE_ROLE_AXIS && !modified && !e->seen_unmodified_query)
  {
	e->seen_unmodified_query = 1;
	r |= core_action_bound(action) ? CORE_QUERY_FIRST_UNMODIFIED : CORE_QUERY_UNBOUND_UNMODIFIED;
  }
  if (role == CORE_ROLE_CLICK)
  {
	if (runtime_nonzero)
	  e->rt_nonzero = 1;
	if (++e->queries >= 600 && !e->rt_nonzero && !e->zero_noted)
	{
	  e->zero_noted = 1;
	  r |= CORE_QUERY_CLICK_ALWAYS_ZERO;
	}
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
  const char *role;
  const TriggerSource *e;
  size_t used = 0;

  if (index < 0 || index >= trigger_table_count)
	return 0;
  e = &trigger_table[index];
  sources[0] = 0;
  for (int i = 0; i < e->nsrc; ++i)
	used += (size_t)snprintf(sources + used, sizeof(sources) - used, "%s%s%s", i ? " " : "", e->src[i],
							 e->rt[i] ? "" : "(suggested)");
  if (!e->nsrc)
	snprintf(sources, sizeof(sources), "UNRESOLVED:no-bound-path-known");
  resolve_role((TriggerSource *)e);
  role = core_role_name(e->role_cached ? e->role : CORE_ROLE_OTHER);
  snprintf(hands, sizeof(hands), "%s%s%s", (e->hands & CORE_HAND_LEFT) ? "l" : "",
		   ((e->hands & CORE_HAND_LEFT) && (e->hands & CORE_HAND_RIGHT)) ? " " : "",
		   (e->hands & CORE_HAND_RIGHT) ? "r" : "");
  {
	char low[XR_MAX_ACTION_NAME_SIZE];
	size_t n;
	int flag;
	for (n = 0; e->name[n] && n < sizeof(low) - 1; ++n)
	  low[n] = (e->name[n] >= 'A' && e->name[n] <= 'Z') ? (char)(e->name[n] + 32) : e->name[n];
	low[n] = 0;
	flag = ci_has(low, "trigger") || ci_has(low, "grip") || ci_has(low, "squeeze") || ci_has(low, "button") || ci_has(low, "click");
	snprintf(buf, size, "ACTION name=\"%s\" handle=%llu type=%s bound=[%s] role=%s method=%s hands=[%s]%s",
			 core_action_name(e->handle), (unsigned long long)e->handle,
			 e->type == XR_ACTION_TYPE_BOOLEAN_INPUT ? "bool" : e->type == XR_ACTION_TYPE_FLOAT_INPUT ? "float" : "other",
			 sources, role, core_role_method_name(e->role_cached ? e->role_method : 0), hands,
			 flag ? " FLAG" : "");
  }
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
