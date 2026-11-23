#pragma once
#include <stddef.h>
#include <openxr/openxr.h>

/* Shared by the legacy proxy (main.c) and the API layer (openxr_input_layer.c).
   Behavior here is the single source of truth; do not reimplement in callers. */

/* Trigger response (analog float path only).
	 v1      = clamp((v - deadzone) / (1 - deadzone), 0, 1)   when deadzone > 0
	 curved  = pow(v1, 1 / exponent)
	 analog  = clamp(curved * gain, 0, 1)
	 boolean = curved >= press_threshold    (threshold is applied to the CURVED value)
   exponent > 1: LESS pull needed (easier). exponent < 1: DEEPER pull needed.
   exponent == 1, deadzone == 0, gain == 1 is the identity. Outputs are always in [0,1].
   NOTE: press_threshold == 0 makes the Boolean always true (value >= 0). */
typedef struct
{
  float deadzone;        /* [0, 0.9], default 0 */
  float exponent;        /* [0.01, 20], default 1 */
  float press_threshold; /* [0, 1], default 0.5 */
  float gain;            /* [0, 100], default 1, analog path only */
  float force_value;     /* -1 = disabled; >=0 delivers exactly this constant (clamped [0,1]) */
  float range_max;       /* (0,1], default 1; raw is divided by this before deadzone/exponent */
  float click_threshold; /* -1 = pass hardware click through; >=0 synthesize click */
  int click_mode;        /* CORE_CLICK_REPLACE | OR | AND */
  int click_compare;     /* CORE_COMPARE_RAWPULL (after deadzone+range, before exponent/gain) | CURVED (after gain) */
} CoreTriggerCfg;
#define CORE_CLICK_REPLACE 0 /* out = hit */
#define CORE_CLICK_OR 1      /* out = runtime || hit */
#define CORE_CLICK_AND 2     /* out = runtime && hit */
#define CORE_COMPARE_RAWPULL 0
#define CORE_COMPARE_CURVED 1
#define CORE_ROLE_OTHER 0
#define CORE_ROLE_AXIS 1
#define CORE_ROLE_CLICK 2
#define CORE_ROLE_TOUCH 3
int core_trigger_classify_path(const char *path, int *hand_flag);
int core_trigger_path_matches(const char *path, int role, int hand_flag);
float core_trigger_pull(float v, const CoreTriggerCfg *cfg);
float core_trigger_click_compare(float raw, const CoreTriggerCfg *cfg);
int core_click_mode_parse(const char *s);
const char *core_click_mode_name(int mode);
int core_click_compare_parse(const char *s);
const char *core_click_compare_name(int compare);
void core_trigger_mark_source_confirmed(XrAction action, const char *source_path);
int core_click_source_confirmed(XrAction action);
int core_trigger_click_enabled(const CoreTriggerCfg *cfg);
int core_trigger_click(float raw, int runtime_click, const CoreTriggerCfg *cfg);

void core_trigger_cfg_defaults(CoreTriggerCfg *cfg);
void core_trigger_cfg_sanitize(CoreTriggerCfg *cfg);
float core_trigger_curve(float v, const CoreTriggerCfg *cfg);
float core_trigger_output(float v, const CoreTriggerCfg *cfg);
int core_trigger_pressed(float raw, const CoreTriggerCfg *cfg);
/* Returns 1 only the first time it is called in this process (caller logs the message). */
int core_trigger_note_negative_threshold(void);

/* Trigger identification by bound source path (/user/hand/<hand>/input/trigger/value). */
#define CORE_HAND_RIGHT 1
#define CORE_HAND_LEFT 2
void core_trigger_reset_sources(void);
void core_trigger_note_action(XrAction action, XrActionType type, const char *name);
int core_trigger_click_flags(XrAction action);
const char *core_click_source_path(XrAction action);
const char *core_action_name(XrAction action);
void core_trigger_set_target(const char *name);
const char *core_trigger_target(void);
int core_trigger_value_action_count(int hand_flag);
int core_trigger_target_excludes(XrAction action);
#define CORE_QUERY_FIRST_CLICK 1
#define CORE_QUERY_FIRST_UNMODIFIED 2
#define CORE_QUERY_CLICK_ALWAYS_ZERO 4
int core_query_note(XrAction action, int modified, int runtime_nonzero);
#define CORE_METHOD_PATH 1
#define CORE_METHOD_SUGGESTED 2
#define CORE_METHOD_NAME 3
int core_action_role(XrAction action, int *hands, int *method);
const char *core_role_name(int role);
const char *core_role_method_name(int method);
void core_analog_store(int hand, float raw, const CoreTriggerCfg *cfg);
int core_analog_cached(int hand, float *analog, float *rawpull);
int core_trigger_click_value(float compare, int runtime_click, const CoreTriggerCfg *cfg);
int core_click_synth(int hand, int runtime_click, const CoreTriggerCfg *cfg, float *compare, int *have_analog);
int core_click_warn_noanalog(XrAction action);
int core_click_changed(XrAction action, int out);
int core_detent_note(XrAction action, int runtime_click); /* 0 none, 1 rising, 2 falling */
int core_pair_format(int hand, char *buf, size_t size);
int core_action_role_format(int index, char *buf, size_t size);
int core_action_table_count(void);
int core_action_table_format(int index, char *buf, size_t size);
void core_trigger_summary(char *buf, size_t size);
int core_click_log_gate(int runtime, int out, unsigned long long now_ms);
void core_trigger_mark_source(XrAction action, const char *source_path);
int core_trigger_hand_flags(XrAction action);
const char *core_trigger_source_path(XrAction action);
int core_trigger_sources_known(void);
XrAction core_trigger_find_float_action(int hand_flag);
/* Returns 1 the first time each identification method (1=source path, 0=action name) is used. */
int core_trigger_method_first_use(int by_source_path);

/* Throttled diagnostic: at most 4 per second, on change or once per second at rest. */
/* Records raw min/max and rising edges (delivered analog 0.0f -> non-zero). Call from float path only. */
void core_trigger_track(float raw, float out);
int core_trigger_log_gate(float raw, float curved, int pressed, unsigned long long now_ms);
void core_trigger_format_diag(char *buf, size_t size, const char *method, const char *action_name,
							  XrAction action, const char *hand, float raw, float curved,
							  float out, int pressed, const CoreTriggerCfg *cfg);

/* Right-hand offset. rot_xyz are radians, stored as config keys x,y,z
   (rotation about OpenXR X, Y, Z respectively). pos_xyz are meters.
   frame_local=0: orientation = rotation * hand (world frame)
   frame_local=1: orientation = hand * rotation (local frame) */
void core_apply_right_offset(XrPosef *pose, const double rot_xyz[3],
							 const double pos_xyz[3], int frame_local);
