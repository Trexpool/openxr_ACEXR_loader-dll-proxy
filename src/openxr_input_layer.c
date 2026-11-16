#include <openxr/openxr.h>
#include <openxr/openxr_loader_negotiation.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Quaternion.h"

#if defined(_WIN32)
#include <windows.h>
#define LAYER_EXPORT __declspec(dllexport)
#else
#define LAYER_EXPORT
#endif

#define ACTION_CAPACITY 128
#define ACTION_SPACE_CAPACITY 64
#define BINDING_REMAP_CAPACITY 32
#define ACTION_BINDING_REMAP_CAPACITY 64
#define DEFAULT_TRIGGER_PRESS_THRESHOLD 0.50f

typedef struct
{
  XrAction handle;
  char name[XR_MAX_ACTION_NAME_SIZE];
  int has_right_subaction;
} ActionMetadata;

typedef struct
{
  char source_path[XR_MAX_PATH_LENGTH];
  char target_path[XR_MAX_PATH_LENGTH];
} BindingRemap;

typedef struct
{
  char action_name[XR_MAX_ACTION_NAME_SIZE];
  char source_path[XR_MAX_PATH_LENGTH];
  char target_path[XR_MAX_PATH_LENGTH];
  int mode; /* 0=replace, 1=add, 2=disable */
} ActionBindingRemap;

typedef struct
{
  float trigger_press_threshold;
  int right_hand_frame_local;
  float offset_rotation_right_x;
  float offset_rotation_right_y;
  float offset_rotation_right_z;
  float offset_position_right_x;
  float offset_position_right_y;
  float offset_position_right_z;
  int binding_remapping_enabled;
  char binding_profile_process[256];
  BindingRemap binding_remaps[BINDING_REMAP_CAPACITY];
  int binding_remap_count;
  ActionBindingRemap action_binding_remaps[ACTION_BINDING_REMAP_CAPACITY];
  int action_binding_remap_count;
} LayerConfig;

static PFN_xrGetInstanceProcAddr next_get_instance_proc_addr;
static PFN_xrCreateApiLayerInstance next_create_api_layer_instance;
static PFN_xrLocateSpace next_locate_space;
static PFN_xrSuggestInteractionProfileBindings next_suggest_bindings;
static PFN_xrGetActionStateBoolean next_get_action_state_boolean;
static PFN_xrGetActionStateFloat next_get_action_state_float;
static PFN_xrCreateAction next_create_action;
static PFN_xrCreateActionSpace next_create_action_space;
static PFN_xrStringToPath next_string_to_path;
static PFN_xrPathToString next_path_to_string;

static ActionMetadata actions[ACTION_CAPACITY];
static uint32_t action_count;
static XrSpace right_hand_action_spaces[ACTION_SPACE_CAPACITY];
static uint32_t right_hand_action_space_count;

static char process_name[MAX_PATH];
static XrPath right_hand_path;
static XrPath left_hand_path;
static char config_path[MAX_PATH];
static FILETIME config_write_time;
static int config_write_time_valid;
static LayerConfig config;
static uint32_t right_pose_correction_log_counter;

static float apply_negative_trigger_sensitivity(float original, float threshold)
{
  float gain;
  float adjusted;
  if (threshold >= 0.0f)
  {
	if (original < threshold)
	  return 0.0f;
	return original;
  }

  gain = 1.0f + (-threshold);
  adjusted = original * gain;
  if (adjusted > 1.0f)
	adjusted = 1.0f;
  if (adjusted < 0.0f)
	adjusted = 0.0f;
  return adjusted;
}

static const char *action_name(XrAction action)
{
  for (uint32_t i = 0; i < action_count; ++i)
  {
	if (actions[i].handle == action)
	  return actions[i].name;
  }
  return "<unknown>";
}

static XrAction action_handle_for_name(const char *name)
{
  for (uint32_t i = 0; i < action_count; ++i)
  {
	if (_stricmp(actions[i].name, name) == 0)
	  return actions[i].handle;
  }
  return XR_NULL_HANDLE;
}

static int action_has_right_subaction(XrAction action)
{
  for (uint32_t i = 0; i < action_count; ++i)
  {
	if (actions[i].handle == action)
	  return actions[i].has_right_subaction;
  }
  return 0;
}

static int is_action_named(XrAction action, const char *name)
{
  return _stricmp(action_name(action), name) == 0;
}

static float deg_to_rad(float value)
{
  return value * (3.14159265358979323846f / 180.0f);
}

static void layer_log(const char *text)
{
  FILE *file = fopen("openxr_api_layer.log", "a");
  if (file)
  {
	fprintf(file, "%s\n", text);
	fclose(file);
  }
}

static void identify_process(void)
{
#if defined(_WIN32)
  char path[MAX_PATH];
  DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
  const char *slash = NULL;
  if (length == 0 || length >= sizeof(path))
	return;
  slash = strrchr(path, '\\');
  if (!slash)
	slash = strrchr(path, '/');
  snprintf(process_name, sizeof(process_name), "%s", slash ? slash + 1 : path);
#endif
}

static char *trim(char *text)
{
  char *end;
  while (*text && isspace((unsigned char)*text))
	++text;
  end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1]))
	--end;
  *end = 0;
  return text;
}

static void set_default_config(void)
{
  memset(&config, 0, sizeof(config));
  config.trigger_press_threshold = DEFAULT_TRIGGER_PRESS_THRESHOLD;
  snprintf(config.binding_profile_process, sizeof(config.binding_profile_process), "%s",
		   "readyornot-win64-shipping.exe");
}

static int binding_profile_is_active(void)
{
  return config.binding_profile_process[0] != 0 &&
		 _stricmp(config.binding_profile_process, process_name) == 0;
}

static int find_binding_remap(const char *source_path, const char **target_path)
{
  for (int i = 0; i < config.binding_remap_count; ++i)
  {
	if (_stricmp(config.binding_remaps[i].source_path, source_path) == 0)
	{
	  *target_path = config.binding_remaps[i].target_path;
	  return 1;
	}
  }
  return 0;
}

static int find_action_binding_remap(const char *action_name_value, const char *source_path,
									 int *mode, const char **target_path)
{
  for (int i = 0; i < config.action_binding_remap_count; ++i)
  {
	ActionBindingRemap *remap = &config.action_binding_remaps[i];
	if (_stricmp(action_name_value, remap->action_name) == 0 &&
		_stricmp(source_path, remap->source_path) == 0)
	{
	  *mode = remap->mode;
	  *target_path = remap->target_path;
	  return 1;
	}
  }
  return 0;
}

static void binding_path_to_string(XrInstance instance, XrPath path, char *buffer, size_t buffer_size)
{
  uint32_t count = 0;
  if (!buffer || buffer_size == 0)
	return;

  buffer[0] = 0;
  if (!next_path_to_string ||
	  next_path_to_string(instance, path, (uint32_t)buffer_size, &count, buffer) != XR_SUCCESS)
  {
	snprintf(buffer, buffer_size, "<path:%llu>", (unsigned long long)path);
  }
}

static void resolve_config_path(void)
{
#if defined(_WIN32)
  char loader_path[MAX_PATH];
  HMODULE loader = GetModuleHandleA("openxr_loader.dll");
  DWORD length;
  char *slash;

  if (config_path[0] != 0 || !loader)
	return;

  length = GetModuleFileNameA(loader, loader_path, sizeof(loader_path));
  if (length == 0 || length >= sizeof(loader_path))
	return;

  snprintf(config_path, sizeof(config_path), "%s", loader_path);
  slash = strrchr(config_path, '\\');
  if (!slash)
	slash = strrchr(config_path, '/');
  if (!slash)
	return;

  snprintf(slash + 1, (size_t)(config_path + sizeof(config_path) - (slash + 1)),
		   "openxr_loader_config.txt");
#endif
}

static void parse_remap_action_command(const char *command, const char *value)
{
  const char *prefix = command;
  int mode = 0;
  const char *path;
  ActionBindingRemap *remap;
  size_t action_len;

  if (strncmp(command, "remap_add_action_", 17) == 0)
  {
	prefix = command + 17;
	mode = 1;
  }
  else if (strncmp(command, "remap_disable_action_", 21) == 0)
  {
	prefix = command + 21;
	mode = 2;
  }
  else
  {
	prefix = command + 13;
  }

  path = strstr(prefix, "_/user/");
  if (!path || config.action_binding_remap_count >= ACTION_BINDING_REMAP_CAPACITY)
	return;

  remap = &config.action_binding_remaps[config.action_binding_remap_count];
  action_len = (size_t)(path - prefix);
  if (action_len >= sizeof(remap->action_name))
	action_len = sizeof(remap->action_name) - 1;

  memcpy(remap->action_name, prefix, action_len);
  remap->action_name[action_len] = 0;
  snprintf(remap->source_path, sizeof(remap->source_path), "%s", path + 1);
  snprintf(remap->target_path, sizeof(remap->target_path), "%s", value);
  remap->mode = mode;
  config.action_binding_remap_count++;
}

static void load_layer_config(void)
{
#if defined(_WIN32)
  WIN32_FILE_ATTRIBUTE_DATA attributes;
  FILE *file;
  char line[512];

  resolve_config_path();
  if (config_path[0] == 0)
	return;

  if (!GetFileAttributesExA(config_path, GetFileExInfoStandard, &attributes))
	return;

  if (config_write_time_valid &&
	  CompareFileTime(&config_write_time, &attributes.ftLastWriteTime) == 0)
	return;

  set_default_config();
  config_write_time = attributes.ftLastWriteTime;
  config_write_time_valid = 1;

  file = fopen(config_path, "r");
  if (!file)
	return;

  while (fgets(line, sizeof(line), file))
  {
	char *sep;
	char *command;
	char *value;
	double number;

	line[strcspn(line, "\r\n")] = 0;
	command = trim(line);
	if (command[0] == 0 || command[0] == '#')
	  continue;

	sep = strchr(command, '=');
	if (!sep)
	  continue;

	*sep = 0;
	value = trim(sep + 1);
	command = trim(command);

	if (strcmp(command, "trigger_press_threshold") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  if (number < -3.0)
		number = -3.0;
	  if (number > 1.0)
		number = 1.0;
	  config.trigger_press_threshold = (float)number;
	  continue;
	}

	if (strcmp(command, "frame") == 0)
	{
	  config.right_hand_frame_local = (_stricmp(value, "local") == 0);
	  continue;
	}

	if (strcmp(command, "right_hand_position_offset_x") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_position_right_x = (float)number;
	  continue;
	}
	if (strcmp(command, "right_hand_position_offset_y") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_position_right_y = (float)number;
	  continue;
	}
	if (strcmp(command, "right_hand_position_offset_z") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_position_right_z = (float)number;
	  continue;
	}
	if (strcmp(command, "right_hand_rotation_offset_x") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_rotation_right_x = deg_to_rad((float)number);
	  continue;
	}
	if (strcmp(command, "right_hand_rotation_offset_y") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_rotation_right_y = deg_to_rad((float)number);
	  continue;
	}
	if (strcmp(command, "right_hand_rotation_offset_z") == 0 && sscanf(value, "%lf", &number) == 1)
	{
	  config.offset_rotation_right_z = deg_to_rad((float)number);
	  continue;
	}

	if (strcmp(command, "remap_enabled") == 0)
	{
	  config.binding_remapping_enabled = (strcmp(value, "1") == 0);
	  continue;
	}

	if (strcmp(command, "profile_process") == 0)
	{
	  snprintf(config.binding_profile_process, sizeof(config.binding_profile_process), "%s", value);
	  continue;
	}

	if (strncmp(command, "remap_source_", 13) == 0)
	{
	  if (config.binding_remap_count < BINDING_REMAP_CAPACITY && value[0] != 0)
	  {
		BindingRemap *remap = &config.binding_remaps[config.binding_remap_count++];
		snprintf(remap->source_path, sizeof(remap->source_path), "%s", command + 13);
		snprintf(remap->target_path, sizeof(remap->target_path), "%s", value);
	  }
	  continue;
	}

	if (strncmp(command, "remap_action_", 13) == 0 ||
		strncmp(command, "remap_add_action_", 17) == 0 ||
		strncmp(command, "remap_disable_action_", 21) == 0)
	{
	  parse_remap_action_command(command, value);
	  continue;
	}
  }

  fclose(file);
#endif
}

static int is_right_action_space(XrSpace space)
{
  for (uint32_t i = 0; i < right_hand_action_space_count; ++i)
  {
	if (right_hand_action_spaces[i] == space)
	  return 1;
  }
  return 0;
}

static void add_right_action_space(XrSpace space)
{
  if (space == XR_NULL_HANDLE || is_right_action_space(space))
	return;

  if (right_hand_action_space_count < ACTION_SPACE_CAPACITY)
	right_hand_action_spaces[right_hand_action_space_count++] = space;
}

static void apply_right_pose_correction(XrSpaceLocation *location)
{
	Quaternion hand;
  Quaternion original_hand;
  Quaternion rotation;
  float rotation_zyx[3];
  float offset[3];
  float rotated[3];

	if ((location->locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0 ||
	  (location->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0)
  {
	return;
  }

	hand.v[Z_AXIS] = location->pose.orientation.z;
  hand.v[Y_AXIS] = location->pose.orientation.y;
  hand.v[X_AXIS] = location->pose.orientation.x;
  hand.w = location->pose.orientation.w;
  Quaternion_copy(&hand, &original_hand);

  rotation_zyx[Z_AXIS] = config.offset_rotation_right_z;
  rotation_zyx[Y_AXIS] = config.offset_rotation_right_y;
  rotation_zyx[X_AXIS] = config.offset_rotation_right_x;
  Quaternion_fromEulerZYX(rotation_zyx, &rotation);

  if (config.right_hand_frame_local)
	Quaternion_multiply(&hand, &rotation, &hand);
  else
	Quaternion_multiply(&rotation, &hand, &hand);

  offset[0] = config.offset_position_right_x;
  offset[1] = config.offset_position_right_y;
  offset[2] = config.offset_position_right_z;

  if (config.right_hand_frame_local)
	Quaternion_rotate(&original_hand, offset, rotated);
  else
	Quaternion_rotate(&rotation, offset, rotated);

  location->pose.position.x += rotated[0];
  location->pose.position.y += rotated[1];
  location->pose.position.z += rotated[2];
	location->pose.orientation.z = hand.v[Z_AXIS];
  location->pose.orientation.y = hand.v[Y_AXIS];
  location->pose.orientation.x = hand.v[X_AXIS];
  location->pose.orientation.w = hand.w;
}

static XrResult XRAPI_CALL layer_create_action(
	XrActionSet actionSet,
	const XrActionCreateInfo *createInfo,
	XrAction *action)
{
  XrResult result = next_create_action(actionSet, createInfo, action);
  if (result == XR_SUCCESS && createInfo && action && action_count < ACTION_CAPACITY)
  {
	int has_right = 0;
	actions[action_count].handle = *action;
	snprintf(actions[action_count].name, sizeof(actions[action_count].name), "%s", createInfo->actionName);

	if (createInfo->subactionPaths && createInfo->countSubactionPaths > 0)
	{
	  for (uint32_t i = 0; i < createInfo->countSubactionPaths; ++i)
	  {
		if (createInfo->subactionPaths[i] == right_hand_path)
		{
		  has_right = 1;
		  break;
		}
	  }
	}

	actions[action_count].has_right_subaction = has_right;
	++action_count;
  }
  return result;
}

static XrResult XRAPI_CALL layer_create_action_space(
	XrSession session,
	const XrActionSpaceCreateInfo *createInfo,
	XrSpace *space)
{
  XrResult result = next_create_action_space(session, createInfo, space);
  if (result == XR_SUCCESS && createInfo && space)
  {
	if (createInfo->subactionPath == right_hand_path ||
		(createInfo->subactionPath == XR_NULL_PATH && action_has_right_subaction(createInfo->action)))
	{
	  add_right_action_space(*space);
	}
  }
  return result;
}

static XrResult XRAPI_CALL layer_suggest_bindings(
	XrInstance instance,
	const XrInteractionProfileSuggestedBinding *suggestedBindings)
{
  char interaction_profile[XR_MAX_PATH_LENGTH] = {0};
  int profile_active;
  XrInteractionProfileSuggestedBinding forwarded;
  XrActionSuggestedBinding *rewritten = NULL;
  uint32_t additional_bindings = 0;
  uint32_t forwarded_count = 0;
  int rewrite_count = 0;
  XrResult result;

  load_layer_config();
  if (!suggestedBindings)
	return next_suggest_bindings(instance, suggestedBindings);

  binding_path_to_string(instance, suggestedBindings->interactionProfile,
						 interaction_profile, sizeof(interaction_profile));

  profile_active = binding_profile_is_active();
  forwarded = *suggestedBindings;

  if (profile_active && config.binding_remapping_enabled)
  {
	for (int i = 0; i < config.action_binding_remap_count; ++i)
	  if (config.action_binding_remaps[i].mode == 1)
		additional_bindings++;
  }

  if (profile_active && config.binding_remapping_enabled && suggestedBindings->countSuggestedBindings > 0)
  {
	rewritten = (XrActionSuggestedBinding *)malloc(
		sizeof(*rewritten) * (suggestedBindings->countSuggestedBindings + additional_bindings));
	if (rewritten)
	{
	  memcpy(rewritten, suggestedBindings->suggestedBindings,
			 sizeof(*rewritten) * suggestedBindings->countSuggestedBindings);
	  forwarded.suggestedBindings = rewritten;
	}
  }

  for (uint32_t i = 0; i < suggestedBindings->countSuggestedBindings; ++i)
  {
	const XrActionSuggestedBinding *binding = &suggestedBindings->suggestedBindings[i];
	const char *replacement = NULL;
	const char *action_replacement = NULL;
	const char *rule_source = "none";
	const char *decision = "forward";
	int action_mode = 0;
	int configured = 0;
	int action_configured = 0;
	int applied = 0;
	char source_path[XR_MAX_PATH_LENGTH];
	char final_path[XR_MAX_PATH_LENGTH];
	char message[768];
	const char *name = action_name(binding->action);

	binding_path_to_string(instance, binding->binding, source_path, sizeof(source_path));
	snprintf(final_path, sizeof(final_path), "%s", source_path);

	if (profile_active && config.binding_remapping_enabled && rewritten)
	{
	  action_configured = find_action_binding_remap(name, source_path, &action_mode, &action_replacement);
	  configured = find_binding_remap(source_path, &replacement);
	}

	if (action_configured && action_mode == 2)
	{
	  rule_source = "action:disable";
	  decision = "dropped";
	  rewrite_count++;
	  snprintf(message, sizeof(message),
			   "remap.binding profile=%s action=%s source=%s final=<dropped> rule=%s decision=%s",
			   interaction_profile,
			   name,
			   source_path,
			   rule_source,
			   decision);
	  layer_log(message);
	  continue;
	}

	if (action_configured && action_mode == 0)
	{
	  configured = 1;
	  replacement = action_replacement;
	  rule_source = "action:replace";
	}
	else if (configured)
	{
	  rule_source = "global:replace";
	}

	if (rewritten)
	  rewritten[forwarded_count] = *binding;

	if (configured && rewritten)
	{
	  XrPath replacement_path = XR_NULL_PATH;
	  if (next_string_to_path(instance, replacement, &replacement_path) == XR_SUCCESS)
	  {
		rewritten[forwarded_count].binding = replacement_path;
		snprintf(final_path, sizeof(final_path), "%s", replacement);
		decision = "rewritten";
		applied = 1;
		rewrite_count++;
	  }
	  else
	  {
		decision = "rewrite_path_lookup_failed";
	  }
	}
	else if (configured && !rewritten)
	{
	  decision = "rewrite_buffer_unavailable";
	}

	snprintf(message, sizeof(message),
			 "remap.binding profile=%s action=%s source=%s final=%s rule=%s decision=%s applied=%d",
			 interaction_profile,
			 name,
			 source_path,
			 final_path,
			 rule_source,
			 decision,
			 applied);
	layer_log(message);

	forwarded_count++;
  }

  if (rewritten && profile_active && config.binding_remapping_enabled)
  {
	for (int i = 0; i < config.action_binding_remap_count; ++i)
	{
	  ActionBindingRemap *remap = &config.action_binding_remaps[i];
	  XrAction action;
	  XrPath path = XR_NULL_PATH;

	  if (remap->mode != 1)
		continue;

	  action = action_handle_for_name(remap->action_name);
	  if (action == XR_NULL_HANDLE)
	  {
		char message[768];
		snprintf(message, sizeof(message),
				 "remap.binding_add profile=%s action=%s source=%s target=%s decision=skipped_unknown_action",
				 interaction_profile,
				 remap->action_name,
				 remap->source_path,
				 remap->target_path);
		layer_log(message);
		continue;
	  }

	  if (next_string_to_path(instance, remap->target_path, &path) != XR_SUCCESS)
	  {
		char message[768];
		snprintf(message, sizeof(message),
				 "remap.binding_add profile=%s action=%s source=%s target=%s decision=skipped_target_lookup_failed",
				 interaction_profile,
				 remap->action_name,
				 remap->source_path,
				 remap->target_path);
		layer_log(message);
		continue;
	  }

	  rewritten[forwarded_count].action = action;
	  rewritten[forwarded_count].binding = path;
	  forwarded_count++;
	  rewrite_count++;
	  {
		char message[768];
		snprintf(message, sizeof(message),
				 "remap.binding_add profile=%s action=%s source=%s target=%s decision=added",
				 interaction_profile,
				 remap->action_name,
				 remap->source_path,
				 remap->target_path);
		layer_log(message);
	  }
	}
  }

  if (rewritten)
	forwarded.countSuggestedBindings = forwarded_count;

	  {
	  char short_profile[64];
	  char short_process[64];
	  char message[512];
	  snprintf(short_profile, sizeof(short_profile), "%.63s", interaction_profile);
	  snprintf(short_process, sizeof(short_process), "%.63s", process_name[0] ? process_name : "<unknown>");
	  snprintf(message, sizeof(message),
			   "xrSuggestInteractionProfileBindings profile=%s process=%s count=%u remap=%d rewrites=%d",
			   short_profile, short_process,
			   suggestedBindings->countSuggestedBindings,
			   profile_active && config.binding_remapping_enabled,
			   rewrite_count);
	  layer_log(message);
	}

  result = next_suggest_bindings(instance, rewritten ? &forwarded : suggestedBindings);
  free(rewritten);
  return result;
}

static XrResult XRAPI_CALL layer_get_action_state_boolean(
	XrSession session,
	const XrActionStateGetInfo *getInfo,
	XrActionStateBoolean *state)
{
  XrResult result = next_get_action_state_boolean(session, getInfo, state);
  XrBool32 original_state = state ? state->currentState : XR_FALSE;

  load_layer_config();
  if (getInfo && state)
  {
	if (result == XR_SUCCESS &&
		is_action_named(getInfo->action, "triggerpressed") &&
		getInfo->subactionPath != XR_NULL_PATH)
	{
	  XrAction trigger_action = action_handle_for_name("trigger");
	  XrActionStateGetInfo trigger_info = {XR_TYPE_ACTION_STATE_GET_INFO};
	  XrActionStateFloat trigger_state = {XR_TYPE_ACTION_STATE_FLOAT};

	  if (trigger_action != XR_NULL_HANDLE)
	  {
		trigger_info.action = trigger_action;
		trigger_info.subactionPath = getInfo->subactionPath;
		if (next_get_action_state_float(session, &trigger_info, &trigger_state) == XR_SUCCESS &&
			trigger_state.isActive)
		{
		  float effective_trigger = apply_negative_trigger_sensitivity(trigger_state.currentState,
													 config.trigger_press_threshold);
		  state->currentState = effective_trigger >= (config.trigger_press_threshold > 0.0f
															 ? config.trigger_press_threshold
															 : 0.0f);
		  state->changedSinceLastSync = trigger_state.changedSinceLastSync;
		  state->lastChangeTime = trigger_state.lastChangeTime;
		}
	  }
	}

	{
	  char message[512];
	  snprintf(message, sizeof(message),
			   "xrGetActionStateBoolean action=%s active=%d original=%d value=%d threshold=%.3f result=%d",
			   action_name(getInfo->action), state->isActive, original_state, state->currentState,
			   config.trigger_press_threshold, result);
	  layer_log(message);
	}
  }
  return result;
}

static XrResult XRAPI_CALL layer_get_action_state_float(
	XrSession session,
	const XrActionStateGetInfo *getInfo,
	XrActionStateFloat *state)
{
  XrResult result = next_get_action_state_float(session, getInfo, state);
  float original_state = state ? state->currentState : 0.0f;

  load_layer_config();
  if (getInfo && state)
  {
	if (result == XR_SUCCESS && state->isActive &&
		is_action_named(getInfo->action, "trigger") &&
		getInfo->subactionPath == right_hand_path)
	{
	  state->currentState = apply_negative_trigger_sensitivity(state->currentState,
										 config.trigger_press_threshold);
	}

	{
	  char message[512];
	  snprintf(message, sizeof(message),
			   "xrGetActionStateFloat action=%s active=%d original=%f value=%f threshold=%.3f result=%d",
			   action_name(getInfo->action), state->isActive, original_state, state->currentState,
			   config.trigger_press_threshold, result);
	  layer_log(message);
	}
  }
  return result;
}

static XrResult XRAPI_CALL layer_locate_space(
	XrSpace space,
	XrSpace baseSpace,
	XrTime time,
	XrSpaceLocation *location)
{
  XrResult result = next_locate_space(space, baseSpace, time, location);
	(void)baseSpace;
  (void)time;

  load_layer_config();
  if (result == XR_SUCCESS && location && location->type == XR_TYPE_SPACE_LOCATION &&
	  is_right_action_space(space))
  {
	XrPosef before = location->pose;
	apply_right_pose_correction(location);
	  right_pose_correction_log_counter++;
	if ((right_pose_correction_log_counter % 120u) == 0u)
	{
	  char message[512];
	  snprintf(message, sizeof(message),
			   "xrLocateSpace right_correction frame=%s before_pos=%.4f,%.4f,%.4f after_pos=%.4f,%.4f,%.4f",
			   config.right_hand_frame_local ? "local" : "world",
			   before.position.x, before.position.y, before.position.z,
			   location->pose.position.x, location->pose.position.y, location->pose.position.z);
	  layer_log(message);
	}
  }

  return result;
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
	XrInstance instance, const char *name, PFN_xrVoidFunction *function);
LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
	const XrInstanceCreateInfo *info, const XrApiLayerCreateInfo *apiLayerInfo,
	XrInstance *instance);
LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
	const XrNegotiateLoaderInfo *loaderInfo, const char *apiLayerName,
	XrNegotiateApiLayerRequest *apiLayerRequest);

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
	XrInstance instance, const char *name, PFN_xrVoidFunction *function)
{
  if (!name || !function)
	return XR_ERROR_VALIDATION_FAILURE;
  if (strcmp(name, "xrGetInstanceProcAddr") == 0)
  {
	*function = (PFN_xrVoidFunction)xrGetInstanceProcAddr;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrCreateApiLayerInstance") == 0)
  {
	*function = (PFN_xrVoidFunction)xrCreateApiLayerInstance;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrCreateAction") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_create_action;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrCreateActionSpace") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_create_action_space;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrSuggestInteractionProfileBindings") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_suggest_bindings;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrGetActionStateBoolean") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_get_action_state_boolean;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrGetActionStateFloat") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_get_action_state_float;
	return XR_SUCCESS;
  }
  if (strcmp(name, "xrLocateSpace") == 0)
  {
	*function = (PFN_xrVoidFunction)layer_locate_space;
	return XR_SUCCESS;
  }
  if (!next_get_instance_proc_addr)
	return XR_ERROR_INITIALIZATION_FAILED;
  return next_get_instance_proc_addr(instance, name, function);
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrCreateApiLayerInstance(
	const XrInstanceCreateInfo *info,
	const XrApiLayerCreateInfo *apiLayerInfo,
	XrInstance *instance)
{
  XrApiLayerCreateInfo chain_info;
  const XrApiLayerNextInfo *next_info;
  XrResult result;
  PFN_xrVoidFunction function = NULL;

  if (!apiLayerInfo || !apiLayerInfo->nextInfo || !instance ||
	  apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
	  apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO)
	return XR_ERROR_INITIALIZATION_FAILED;

  next_info = apiLayerInfo->nextInfo;
  next_get_instance_proc_addr = next_info->nextGetInstanceProcAddr;
  next_create_api_layer_instance = next_info->nextCreateApiLayerInstance;
  chain_info = *apiLayerInfo;
  chain_info.nextInfo = next_info->next;

  set_default_config();
  memset(config_path, 0, sizeof(config_path));
  config_write_time_valid = 0;
  action_count = 0;
  right_hand_action_space_count = 0;
  right_pose_correction_log_counter = 0;

  identify_process();
  layer_log("OpenXR input layer initialized");

  result = next_create_api_layer_instance(info, &chain_info, instance);
  if (result != XR_SUCCESS)
	return result;

  if (next_get_instance_proc_addr(*instance, "xrCreateAction", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_create_action = (PFN_xrCreateAction)function;

  if (next_get_instance_proc_addr(*instance, "xrCreateActionSpace", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_create_action_space = (PFN_xrCreateActionSpace)function;

  if (next_get_instance_proc_addr(*instance, "xrSuggestInteractionProfileBindings", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_suggest_bindings = (PFN_xrSuggestInteractionProfileBindings)function;

  if (next_get_instance_proc_addr(*instance, "xrGetActionStateBoolean", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_get_action_state_boolean = (PFN_xrGetActionStateBoolean)function;

  if (next_get_instance_proc_addr(*instance, "xrGetActionStateFloat", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_get_action_state_float = (PFN_xrGetActionStateFloat)function;

  if (next_get_instance_proc_addr(*instance, "xrLocateSpace", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_locate_space = (PFN_xrLocateSpace)function;

  if (next_get_instance_proc_addr(*instance, "xrStringToPath", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_string_to_path = (PFN_xrStringToPath)function;

  if (next_get_instance_proc_addr(*instance, "xrPathToString", &function) != XR_SUCCESS)
	return XR_ERROR_INITIALIZATION_FAILED;
  next_path_to_string = (PFN_xrPathToString)function;

  if (next_string_to_path(*instance, "/user/hand/right", &right_hand_path) != XR_SUCCESS)
	right_hand_path = XR_NULL_PATH;
  if (next_string_to_path(*instance, "/user/hand/left", &left_hand_path) != XR_SUCCESS)
	left_hand_path = XR_NULL_PATH;

  load_layer_config();
  return XR_SUCCESS;
}

LAYER_EXPORT XRAPI_ATTR XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
	const XrNegotiateLoaderInfo *loaderInfo,
	const char *apiLayerName,
	XrNegotiateApiLayerRequest *apiLayerRequest)
{
  (void)apiLayerName;
  if (!loaderInfo || !apiLayerRequest ||
	  loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
	  loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
	return XR_ERROR_INITIALIZATION_FAILED;
  apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
  apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
  apiLayerRequest->getInstanceProcAddr = xrGetInstanceProcAddr;
  apiLayerRequest->createApiLayerInstance = xrCreateApiLayerInstance;
  return XR_SUCCESS;
}
