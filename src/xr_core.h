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
  float click_threshold; /* -1 = pass hardware click through; >=0 synthesize click = curved >= t */
} CoreTriggerCfg;
int core_trigger_click_enabled(const CoreTriggerCfg *cfg);
int core_trigger_click(float raw, const CoreTriggerCfg *cfg);

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
int core_query_note(XrAction action, int modified);
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
