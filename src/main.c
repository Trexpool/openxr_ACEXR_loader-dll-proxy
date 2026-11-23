/*
    Copyright (C) 2022 Aruï¿½ Metello <aruametello@gmail.com>

    Permission to use, copy, modify, and/or distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.


    its easier to find me at discord: osso#6389


    OpenXR proxy dll - adds controler rotation offsets.


    * what is the point of this tool/modification
    allow tweakable controler rotation offsets on theoretically any game running openXR.
    Personally i think all games should have those tweaks within the options the same
    way all mouse input games have sensitivity settings.

    * how does this proxy dll work?
    Rewrites some of the communication between the game and the openXR api, basically the api reads
    the real controler data, adds/subtracts the desired offsets and then passes the data to the game.

    * can this interfere with cheat protected games. (i.e. EAC)
    yes... i do not recomend to attempt usage on those.
    (they probably wont ban you but... who knows)

    * Why all functions are being "proxied by hand" ?
    Usually i use the awesome "wrappit" tool to automatically generate C code of
    a proxy dll without hassle and as close to no overhead you can get (naked pure assembly call of a jump)

    you can find wrappit here: https://www.codeproject.com/Articles/16541/Create-your-Proxy-DLLs-automatically

    but the "fine" folks at microsoft decided that "inline assembly" is bad and those functions
    are not supported anymore with their C/CPP compiler when targeting x86_64 or ARM code, so i went for the
    retarded route and wrote all calls by hand, since this dll has just a few (about 55 of them)

    on the other hand if anyone else wants to take a peek/poke at anything else, is probably just a few lines
    of code away from this starting point.

    OVR Advanced Settings perhalps could integrate IPC comunication with a tool like this to show a cool
    interface to tweak OpenXR stuff. (pretty much anything since the whole interface can be hijacked here)
*/

#define _WIN32_WINNT 0x0A00

#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <openxr\openxr.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "gui.h"
#include "xr_core.h"

// uses https://github.com/MartinWeigel/Quaternion for quaternions because i suck at math
#include "Quaternion.h"

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)

#pragma pack(1)

HINSTANCE hLThis = 0;
HINSTANCE hL = 0;

#define jump_table_size 2048
#define jump_table_name_size 64
PFN_xrVoidFunction *jump_table;
char *jump_table_names;
FILE *h_log = 0;

char root_folder[256];      // workdir, OG dll and config file location.
char config_file_path[256]; // fullpath of the config file
char process_executable_basename[256];

#define BINDING_REMAP_CAPACITY 32
#define ACTION_BINDING_REMAP_CAPACITY 64
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
  int mode;
} ActionBindingRemap;

BindingRemap binding_remaps[BINDING_REMAP_CAPACITY];
int binding_remap_count = 0;
ActionBindingRemap action_binding_remaps[ACTION_BINDING_REMAP_CAPACITY];
int action_binding_remap_count = 0;
int binding_logging_enabled = 1;
int binding_remapping_enabled = 0;
char binding_profile_process[256] = "readyornot-win64-shipping.exe";
double trigger_press_threshold = 0.50;
double trigger_deadzone = 0.0;
double trigger_curve_exponent = 1.0;
double trigger_gain = 1.0;
double trigger_force_value = -1.0;
double trigger_click_threshold = -1.0;
int trigger_click_mode = CORE_CLICK_REPLACE;
int trigger_click_compare = CORE_COMPARE_RAWPULL;
double trigger_range_max = 1.0;
char trigger_target_action[128] = "";
volatile LONG action_dump_requested = 0;

/*
    some default values for the offsets

*/
double offset_rotation_left_x = 0.0;
double offset_rotation_left_y = 0.0;
double offset_rotation_left_z = 0.0;

double offset_rotation_right_x = 0.0;
double offset_rotation_right_y = 0.0;
double offset_rotation_right_z = 0.0;

double offset_position_left_x = 0.0;
double offset_position_left_y = 0.0;
double offset_position_left_z = 0.0;

double offset_position_right_x = 0.0;
double offset_position_right_y = 0.0;
double offset_position_right_z = 0.0;

int right_hand_frame_local = 0;
int capture_ads_apply_position = 0;

// those are not constant, and will be corrected by xrStringToPath as needed.
XrPath id_user_hand_left = 9;
XrPath id_user_hand_right = 10;

/*
    aparently unity does initialize all controlers that it knows regardless
    of availability in the system.

    since the user might be a layman, just store all possible controlers
    on a "left hand" and "right hand" list, if any of those are found, apply offsets.

    if not, the user would have to know what controler he does have to assign
    the offsets only to it.

    knows controler types so far:
        htcvivecontroller - vive wands? no idea
        khrsimplecontroller - sounds like a skeleton driver
        microsoftmotioncontroller - probaly windows mixed reality
        oculustouchcontroller - oculus touch (any version?)
        valveindexcontroller - valve knuckles

*/
int action_space_list_hand_left_cnt = 0;
XrSpace action_space_list_hand_left[64];
int action_space_list_hand_right_cnt = 0;
XrSpace action_space_list_hand_right[64];
int action_space_list_hand_right_kind[64];

int ref_action_space_list_hand_left_cnt = 0;
XrAction ref_action_space_list_hand_left[64];
int ref_action_space_list_hand_right_cnt = 0;
XrAction ref_action_space_list_hand_right[64];
int action_space_list_hand_right_aim_cnt = 0;
XrSpace action_space_list_hand_right_aim[64];

XrAction action_metadata[64];
char action_metadata_names[64][XR_MAX_ACTION_NAME_SIZE];
int action_metadata_cnt = 0;

CRITICAL_SECTION pose_snapshot_lock;
XrPosef latest_head_pose;
XrSpace latest_head_base_space = XR_NULL_HANDLE;
int latest_head_pose_valid = 0;
PoseDisplaySnapshot pose_display_left = {0};
PoseDisplaySnapshot pose_display_right = {0};
volatile LONG pose_capture_active = 0;
volatile LONG pose_capture_left = 0;
volatile LONG pose_capture_right = 0;

#define ACTION_SPACE_LIST_CAPACITY 64

int log_message(const char *fmt, ...);
static XrInstance diag_instance = XR_NULL_HANDLE;
static void proxy_dump_action_table(const char *why);
static void proxy_exit_summary(void)
{
  char line[256];
  core_trigger_summary(line, sizeof(line));
  log_message("%s", line);
}
static Quaternion quaternion_from_xr(const XrQuaternionf *orientation)
{
  Quaternion result;
  result.v[Z_AXIS] = orientation->z;
  result.v[Y_AXIS] = orientation->y;
  result.v[X_AXIS] = orientation->x;
  result.w = orientation->w;
  return result;
}

static int right_action_space_kind(XrSpace space)
{
  for (int c = 0; c < action_space_list_hand_right_cnt; c++)
  {
    if (action_space_list_hand_right[c] == space)
      return action_space_list_hand_right_kind[c];
  }
  return 0;
}

static void proxy_trigger_cfg(CoreTriggerCfg *cfg)
{
  cfg->deadzone = (float)trigger_deadzone;
  cfg->exponent = (float)trigger_curve_exponent;
  cfg->press_threshold = (float)trigger_press_threshold;
  cfg->gain = (float)trigger_gain;
  cfg->force_value = (float)trigger_force_value;
  cfg->range_max = (float)trigger_range_max;
  cfg->click_threshold = (float)trigger_click_threshold;
  cfg->click_mode = trigger_click_mode;
  cfg->click_compare = trigger_click_compare;
  core_trigger_cfg_sanitize(cfg);
}

static void update_pose_display_snapshot(int left, XrSpace base_space, const XrPosef *controller_pose)
{
  if (!InterlockedCompareExchange(&pose_capture_active, 0, 0))
    return;

  volatile LONG *captured = left ? &pose_capture_left : &pose_capture_right;
  if (InterlockedCompareExchange(captured, 0, 0))
    return;

  PoseDisplaySnapshot *snapshot = left ? &pose_display_left : &pose_display_right;
  EnterCriticalSection(&pose_snapshot_lock);

#ifdef BASE_SPACE_DIAGNOSTIC
  Quaternion controller = quaternion_from_xr(&controller_pose->orientation);
  float base_space_rotation[3];
  Quaternion_toEulerZYX(&controller, base_space_rotation);

  snapshot->rotation_x = radToDeg(base_space_rotation[X_AXIS]);
  snapshot->rotation_y = radToDeg(base_space_rotation[Y_AXIS]);
  snapshot->rotation_z = radToDeg(base_space_rotation[Z_AXIS]);
  snapshot->position_x = controller_pose->position.x;
  snapshot->position_y = controller_pose->position.y;
  snapshot->position_z = controller_pose->position.z;
  snapshot->valid = 1;
  InterlockedExchange(captured, 1);
  if (InterlockedCompareExchange(&pose_capture_left, 0, 0) &&
      InterlockedCompareExchange(&pose_capture_right, 0, 0))
    InterlockedExchange(&pose_capture_active, 0);
  LeaveCriticalSection(&pose_snapshot_lock);
  return;
#else
  if (!latest_head_pose_valid || latest_head_base_space != base_space)
  {
    snapshot->valid = 0;
    LeaveCriticalSection(&pose_snapshot_lock);
    return;
  }

  Quaternion head = quaternion_from_xr(&latest_head_pose.orientation);
  Quaternion controller = quaternion_from_xr(&controller_pose->orientation);
  Quaternion inverse_head;
  Quaternion relative_orientation;
  Quaternion_conjugate(&head, &inverse_head);
  Quaternion_multiply(&inverse_head, &controller, &relative_orientation);

  float relative_position_input[3] = {
      controller_pose->position.x - latest_head_pose.position.x,
      controller_pose->position.y - latest_head_pose.position.y,
      controller_pose->position.z - latest_head_pose.position.z};
  float relative_position[3];
  Quaternion_rotate(&inverse_head, relative_position_input, relative_position);

  float relative_rotation[3];
  Quaternion_toEulerZYX(&relative_orientation, relative_rotation);

  {
    Quaternion implied;
    Quaternion_conjugate(&relative_orientation, &implied);
    log_message("CAPTURE_DIAG space=%llu kind=%d head_xyzw=%.6f,%.6f,%.6f,%.6f controller_xyzw=%.6f,%.6f,%.6f,%.6f "
                "relative_xyzw=%.6f,%.6f,%.6f,%.6f implied_offset_xyzw=%.6f,%.6f,%.6f,%.6f",
                (unsigned long long)latest_head_base_space, left ? -1 : 0,
                latest_head_pose.orientation.x, latest_head_pose.orientation.y,
                latest_head_pose.orientation.z, latest_head_pose.orientation.w,
                controller_pose->orientation.x, controller_pose->orientation.y,
                controller_pose->orientation.z, controller_pose->orientation.w,
                relative_orientation.v[X_AXIS], relative_orientation.v[Y_AXIS],
                relative_orientation.v[Z_AXIS], relative_orientation.w,
                implied.v[X_AXIS], implied.v[Y_AXIS], implied.v[Z_AXIS], implied.w);
  }

  snapshot->rotation_x = radToDeg(relative_rotation[X_AXIS]);
  snapshot->rotation_y = radToDeg(relative_rotation[Y_AXIS]);
  snapshot->rotation_z = radToDeg(relative_rotation[Z_AXIS]);
  snapshot->position_x = relative_position[X_AXIS];
  snapshot->position_y = relative_position[Y_AXIS];
  snapshot->position_z = relative_position[Z_AXIS];
  snapshot->valid = 1;
  InterlockedExchange(captured, 1);
  if (InterlockedCompareExchange(&pose_capture_left, 0, 0) &&
      InterlockedCompareExchange(&pose_capture_right, 0, 0))
    InterlockedExchange(&pose_capture_active, 0);
  LeaveCriticalSection(&pose_snapshot_lock);
#endif
}

void request_pose_capture(void)
{
  EnterCriticalSection(&pose_snapshot_lock);
  pose_display_left.valid = 0;
  pose_display_right.valid = 0;
  InterlockedExchange(&pose_capture_left, 0);
  InterlockedExchange(&pose_capture_right, 0);
  InterlockedExchange(&pose_capture_active, 1);
  LeaveCriticalSection(&pose_snapshot_lock);
  log_message("On-demand pose capture armed. right_spaces=%d right_aim_spaces=%d", action_space_list_hand_right_cnt, action_space_list_hand_right_aim_cnt);
}

int get_pose_display_snapshot(int left, PoseDisplaySnapshot *snapshot)
{
  if (!snapshot)
    return 0;

  EnterCriticalSection(&pose_snapshot_lock);
  *snapshot = left ? pose_display_left : pose_display_right;
  LeaveCriticalSection(&pose_snapshot_lock);
  return snapshot->valid;
}

int apply_captured_ads_calibration(void)
{
  PoseDisplaySnapshot right;
  Quaternion captured_relative;
  Quaternion desired_offset;
  float captured_zyx[3];
  float offset_zyx[3];

  EnterCriticalSection(&pose_snapshot_lock);
  right = pose_display_right;
  LeaveCriticalSection(&pose_snapshot_lock);

  if (!right.valid)
  {
    log_message("ADS calibration apply skipped: missing right-hand snapshot.");
    return 0;
  }

  /*
      Snapshot rotations are headset-relative Euler ZYX angles in degrees.
      We need the true inverse rotation (quaternion conjugate), not per-axis negation.
  */
  captured_zyx[X_AXIS] = degToRad(right.rotation_x);
  captured_zyx[Y_AXIS] = degToRad(right.rotation_y);
  captured_zyx[Z_AXIS] = degToRad(right.rotation_z);
  Quaternion_fromEulerZYX(captured_zyx, &captured_relative);
  Quaternion_conjugate(&captured_relative, &desired_offset);
  Quaternion_toEulerZYX(&desired_offset, offset_zyx);

  offset_rotation_right_x = offset_zyx[X_AXIS];
  offset_rotation_right_y = offset_zyx[Y_AXIS];
  offset_rotation_right_z = offset_zyx[Z_AXIS];
  if (capture_ads_apply_position)
  {
    offset_position_right_x = right.position_x;
    offset_position_right_y = right.position_y;
    offset_position_right_z = right.position_z;
  }
  else
  {
    offset_position_right_x = 0.0;
    offset_position_right_y = 0.0;
    offset_position_right_z = 0.0;
  }

  write_config(config_file_path);

  log_message("ADS right-hand calibration applied. mode=%s right_capture_rot=%.2f,%.2f,%.2f right_capture_pos=%.3f,%.3f,%.3f",
              capture_ads_apply_position ? "rotation+position" : "rotation-only",
              right.rotation_x, right.rotation_y, right.rotation_z,
              right.position_x, right.position_y, right.position_z);
  log_message("ADS right-hand calibration offsets set. right_offset_rot=%.2f,%.2f,%.2f",
              radToDeg(offset_rotation_right_x),
              radToDeg(offset_rotation_right_y),
              radToDeg(offset_rotation_right_z));

  return 1;
}

/*
    but beware!
    the idea above is dumb and will apply offsets to anything that
    has the subspace /user/hand/left or /user/hand/right
*/

int log_message(const char *fmt, ...)
{
  // log file is not opened?
  if (!h_log)
    return 1;

  char buffer[8192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

#ifdef DEBUG
  printf("%s\n", buffer);
  return 1;
#endif // debug

  {
    static long log_bytes = 0;
    if (log_bytes > 20L * 1024L * 1024L)
      return 1;
    log_bytes += (long)strlen(buffer) + 1;
  }
  fprintf(h_log, "%s\n", buffer);
  fflush(h_log);

  return 1;
}

/*
    Read the configuration file
*/
void str_lowercase(char *str)
{
  for (int i = 0; str[i]; i++)
    str[i] = tolower(str[i]);
}
int read_config(char *config_file)
{
  double double_temp;
  char line[512];
  FILE *arq = fopen(config_file, "r");

  if (!arq)
  {
    log_message("Could not find my config file at:%s!\n We are trying to run with default offsets now(all zeroes)\n", config_file);
    write_config(config_file); // write a new config file
    return 1;
  }

  while (fgets(line, sizeof(line), arq) != NULL)
  {
    char cmd[512] = {0};
    char *value;
    char *comment;

    line[strcspn(line, "\r\n")] = 0;

    comment = strchr(line, '#');
    if (comment)
      *comment = 0;

    while (isspace((unsigned char)*line))
      memmove(line, line + 1, strlen(line));

    if (line[0] == 0)
      continue;

    str_lowercase(line);

    value = strchr(line, '=');
    if (!value)
    {
      fclose(arq);
      log_message("Invalid line found in the config file:%s!\n Some settings might reset to defaults!\n", config_file);
      write_config(config_file); // write a new config file
      return 1;
    }

    *value = 0;
    value++;

    // trim trailing spaces in key
    {
      size_t key_len = strlen(line);
      while (key_len > 0 && isspace((unsigned char)line[key_len - 1]))
      {
        line[key_len - 1] = 0;
        key_len--;
      }
    }

    while (isspace((unsigned char)*value))
      value++;

    {
      size_t cmd_len = strnlen(line, sizeof(cmd) - 1);
      memcpy(cmd, line, cmd_len);
      cmd[cmd_len] = 0;
    }

    if (strcmp(cmd, "frame") == 0)
    {
      if (strcmp(value, "local") == 0)
        right_hand_frame_local = 1;
      else if (strcmp(value, "world") == 0)
        right_hand_frame_local = 0;
      else
      {
        fclose(arq);
        log_message("Invalid frame value found in the config file:%s!\n", config_file);
        write_config(config_file);
        return 1;
      }
      continue;
    }

    if (strcmp(cmd, "capture_ads_apply_position") == 0)
    {
      capture_ads_apply_position = (strcmp(value, "1") == 0);
      continue;
    }

    if (strcmp(cmd, "capture_ads_apply_position") == 0)
    {
      capture_ads_apply_position = (strcmp(value, "1") == 0);
      continue;
    }

    if (strncmp(cmd, "remap_action_", 13) == 0 ||
        strncmp(cmd, "remap_add_action_", 17) == 0 ||
        strncmp(cmd, "remap_disable_action_", 21) == 0)
    {
      const char *prefix = cmd;
      int mode = 0;
      if (strncmp(cmd, "remap_add_action_", 17) == 0)
      {
        prefix = cmd + 17;
        mode = 1;
      }
      else if (strncmp(cmd, "remap_disable_action_", 21) == 0)
      {
        prefix = cmd + 21;
        mode = 2;
      }
      else
      {
        prefix = cmd + 13;
      }

      const char *path = strstr(prefix, "_/user/");
      if (action_binding_remap_count < ACTION_BINDING_REMAP_CAPACITY && path)
      {
        ActionBindingRemap *remap = &action_binding_remaps[action_binding_remap_count];
        size_t action_len = (size_t)(path - prefix);
        if (action_len >= sizeof(remap->action_name))
          action_len = sizeof(remap->action_name) - 1;
        memcpy(remap->action_name, prefix, action_len);
        remap->action_name[action_len] = 0;
        strncpy(remap->source_path, path + 1, sizeof(remap->source_path) - 1);
        remap->source_path[sizeof(remap->source_path) - 1] = 0;
        strncpy(remap->target_path, value, sizeof(remap->target_path) - 1);
        remap->target_path[sizeof(remap->target_path) - 1] = 0;
        remap->mode = mode;
        action_binding_remap_count++;
      }
      continue;
    }

    if (strcmp(cmd, "trigger_press_threshold") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1)
      {
        if (double_temp < 0.0 && core_trigger_note_negative_threshold())
          log_message("WARNING: negative threshold is not a sensitivity; use trigger_gain=N.N for gain and threshold=0..1 for the cutoff (value %f clamped to 0 in memory; file untouched)", double_temp);
        trigger_press_threshold = double_temp < 0.0 ? 0.0 : (double_temp > 1.0 ? 1.0 : double_temp);
      }
      continue;
    }

    if (strcmp(cmd, "trigger_force_value") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1)
        trigger_force_value = double_temp < 0.0 ? -1.0 : (double_temp > 1.0 ? 1.0 : double_temp);
      continue;
    }

    if (strcmp(cmd, "trigger_click_threshold") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1)
        trigger_click_threshold = double_temp < 0.0 ? -1.0 : (double_temp > 1.0 ? 1.0 : double_temp);
      continue;
    }

    if (strcmp(cmd, "click_mode") == 0)
    {
      int m = core_click_mode_parse(value);
      if (m >= 0)
        trigger_click_mode = m;
      continue;
    }

    if (strcmp(cmd, "click_compare") == 0)
    {
      int m = core_click_compare_parse(value);
      if (m >= 0)
        trigger_click_compare = m;
      continue;
    }

    if (strcmp(cmd, "trigger_range_max") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1 && double_temp > 0.0)
        trigger_range_max = double_temp > 1.0 ? 1.0 : double_temp;
      continue;
    }

    if (strcmp(cmd, "trigger_target_action") == 0)
    {
      strncpy(trigger_target_action, value, sizeof(trigger_target_action) - 1);
      trigger_target_action[sizeof(trigger_target_action) - 1] = 0;
      core_trigger_set_target(trigger_target_action);
      continue;
    }

    if (strcmp(cmd, "trigger_gain") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1 && double_temp >= 0.0)
        trigger_gain = double_temp > 100.0 ? 100.0 : double_temp;
      continue;
    }

    if (strcmp(cmd, "trigger_deadzone") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1)
        trigger_deadzone = double_temp < 0.0 ? 0.0 : (double_temp > 0.9 ? 0.9 : double_temp);
      continue;
    }

    if (strcmp(cmd, "trigger_curve_exponent") == 0)
    {
      if (sscanf(value, "%lf", &double_temp) == 1 && double_temp > 0.0)
        trigger_curve_exponent = double_temp;
      continue;
    }

    if (strcmp(cmd, "binding_log") == 0)
    {
      binding_logging_enabled = (strcmp(value, "0") != 0);
      continue;
    }

    if (strcmp(cmd, "remap_enabled") == 0)
    {
      binding_remapping_enabled = (strcmp(value, "1") == 0);
      continue;
    }

    if (strcmp(cmd, "profile_process") == 0)
    {
      strncpy(binding_profile_process, value, sizeof(binding_profile_process) - 1);
      binding_profile_process[sizeof(binding_profile_process) - 1] = 0;
      continue;
    }

    if (strncmp(cmd, "remap_source_", 13) == 0)
    {
      if (binding_remap_count < BINDING_REMAP_CAPACITY && value[0] != 0)
      {
        BindingRemap *remap = &binding_remaps[binding_remap_count];
        strncpy(remap->source_path, cmd + 13, sizeof(remap->source_path) - 1);
        remap->source_path[sizeof(remap->source_path) - 1] = 0;
        strncpy(remap->target_path, value, sizeof(remap->target_path) - 1);
        remap->target_path[sizeof(remap->target_path) - 1] = 0;
        binding_remap_count++;
      }
      continue;
    }

    if (sscanf(value, "%lf", &double_temp) != 1)
    {
      fclose(arq);
      log_message("Invalid value found in the config file:%s!\n Some settings might reset to defaults!\n", config_file);
      write_config(config_file); // write a new config file
      return 1;
    }

    if (strcmp(cmd, "left_hand_position_offset_x") == 0)
    {
      offset_position_left_x = double_temp;
    }
    else if (strcmp(cmd, "left_hand_position_offset_y") == 0)
    {
      offset_position_left_y = double_temp;
    }
    else if (strcmp(cmd, "left_hand_position_offset_z") == 0)
    {
      offset_position_left_z = double_temp;
    }
    else if (strcmp(cmd, "right_hand_position_offset_x") == 0)
    {
      offset_position_right_x = double_temp;
    }
    else if (strcmp(cmd, "right_hand_position_offset_y") == 0)
    {
      offset_position_right_y = double_temp;
    }
    else if (strcmp(cmd, "right_hand_position_offset_z") == 0)
    {
      offset_position_right_z = double_temp;
    }
    else if (strcmp(cmd, "right_hand_rotation_offset_x") == 0)
    {
      offset_rotation_right_x = degToRad(double_temp);
    }
    else if (strcmp(cmd, "right_hand_rotation_offset_y") == 0)
    {
      offset_rotation_right_y = degToRad(double_temp);
    }
    else if (strcmp(cmd, "right_hand_rotation_offset_z") == 0)
    {
      offset_rotation_right_z = degToRad(double_temp);
    }
    else if (strcmp(cmd, "left_hand_rotation_offset_x") == 0)
    {
      offset_rotation_left_x = degToRad(double_temp);
    }
    else if (strcmp(cmd, "left_hand_rotation_offset_y") == 0)
    {
      offset_rotation_left_y = degToRad(double_temp);
    }
    else if (strcmp(cmd, "left_hand_rotation_offset_z") == 0)
    {
      offset_rotation_left_z = degToRad(double_temp);
    }
    else
    {
      fclose(arq);
      log_message("Invalid line found in the config file:%s!\n Some settings might reset to defaults!\n", config_file);
      write_config(config_file); // write a new config file
      return 1;
    }
  }

  fclose(arq);
  return 1;
}

int write_config(const char *config_file)
{
  FILE *arq = fopen(config_file, "w");
  if (arq)
  {
    fprintf(arq, "#offsets here are written in degrees!\n");
    fprintf(arq, "frame=%s\n", right_hand_frame_local ? "local" : "world");
    fprintf(arq, "left_hand_rotation_offset_x=%f\n", radToDeg(offset_rotation_left_x));
    fprintf(arq, "left_hand_rotation_offset_y=%f\n", radToDeg(offset_rotation_left_y));
    fprintf(arq, "left_hand_rotation_offset_z=%f\n", radToDeg(offset_rotation_left_z));

    fprintf(arq, "right_hand_rotation_offset_x=%f\n", radToDeg(offset_rotation_right_x));
    fprintf(arq, "right_hand_rotation_offset_y=%f\n", radToDeg(offset_rotation_right_y));
    fprintf(arq, "right_hand_rotation_offset_z=%f\n", radToDeg(offset_rotation_right_z));

    fprintf(arq, "left_hand_position_offset_x=%f\n", offset_position_left_x);
    fprintf(arq, "left_hand_position_offset_y=%f\n", offset_position_left_y);
    fprintf(arq, "left_hand_position_offset_z=%f\n", offset_position_left_z);

    fprintf(arq, "right_hand_position_offset_x=%f\n", offset_position_right_x);
    fprintf(arq, "right_hand_position_offset_y=%f\n", offset_position_right_y);
    fprintf(arq, "right_hand_position_offset_z=%f\n", offset_position_right_z);
    fprintf(arq, "\n# OpenXR binding diagnostics/profile\n");
    fprintf(arq, "binding_log=%d\n", binding_logging_enabled ? 1 : 0);
    fprintf(arq, "remap_enabled=%d\n", binding_remapping_enabled ? 1 : 0);
    fprintf(arq, "profile_process=%s\n", binding_profile_process);
    fprintf(arq, "# Boolean press threshold (0-1). Some games fire from the analog axis instead; for those, use trigger_deadzone and trigger_curve_exponent. 0.0 makes the Boolean permanently pressed.\n");
    fprintf(arq, "trigger_press_threshold=%f\n", trigger_press_threshold);
    fprintf(arq, "trigger_deadzone=%f\n", trigger_deadzone);
    fprintf(arq, "trigger_curve_exponent=%f\n", trigger_curve_exponent);
    fprintf(arq, "trigger_gain=%f\n", trigger_gain);
    fprintf(arq, "# -1 = disabled. >=0 delivers exactly this analog value (bisect the game's fire threshold).\n");
    fprintf(arq, "trigger_force_value=%f\n", trigger_force_value);
    fprintf(arq, "# -1 = use hardware click. 0-1 = synthesize click (see click_mode / click_compare).\n");
    fprintf(arq, "trigger_click_threshold=%f\n", trigger_click_threshold);
    fprintf(arq, "# replace: out = hit | or: out = runtime_click || hit | and: out = runtime_click && hit\n");
    fprintf(arq, "click_mode=%s\n", core_click_mode_name(trigger_click_mode));
    fprintf(arq, "# rawpull: compare after deadzone+range, before exponent/gain | curved: compare the curved+gain value\n");
    fprintf(arq, "click_compare=%s\n", core_click_compare_name(trigger_click_compare));
    fprintf(arq, "trigger_range_max=%f\n", trigger_range_max);
    fprintf(arq, "trigger_target_action=%s\n", trigger_target_action);
    fprintf(arq, "capture_ads_apply_position=%d\n", capture_ads_apply_position ? 1 : 0);
    fprintf(arq, "# Add remap_source_<original_path>=<target_path> entries after paths are verified.\n");
    fprintf(arq, "# Gunman right-hand digital B/grip swap:\n");
    fprintf(arq, "# remap_source_/user/hand/right/input/b/click=/user/hand/right/input/squeeze/click\n");
    fprintf(arq, "# remap_source_/user/hand/right/input/squeeze/click=/user/hand/right/input/b/click\n");
    fprintf(arq, "# Action-aware rules can preserve A while moving Gunman's actions:\n");
    fprintf(arq, "# remap_add_action_primarybutton_/user/hand/right/input/squeeze/click=/user/hand/right/input/squeeze/click\n");
    fprintf(arq, "# remap_disable_action_secondarybutton_/user/hand/right/input/b/click=/user/hand/right/input/b/click\n");
    fprintf(arq, "# remap_action_grip_/user/hand/right/input/squeeze/click=/user/hand/right/input/b/click\n");
    fprintf(arq, "# remap_action_grippressed_/user/hand/right/input/squeeze/click=/user/hand/right/input/b/click\n");
    for (int c = 0; c < binding_remap_count; c++)
      fprintf(arq, "remap_source_%s=%s\n", binding_remaps[c].source_path, binding_remaps[c].target_path);
    for (int c = 0; c < action_binding_remap_count; c++)
    {
      const ActionBindingRemap *remap = &action_binding_remaps[c];
      if (remap->mode == 1)
        fprintf(arq, "remap_add_action_%s_%s=%s\n", remap->action_name, remap->source_path, remap->target_path);
      else if (remap->mode == 2)
        fprintf(arq, "remap_disable_action_%s_%s=%s\n", remap->action_name, remap->source_path, remap->target_path);
      else
        fprintf(arq, "remap_action_%s_%s=%s\n", remap->action_name, remap->source_path, remap->target_path);
    }

    fclose(arq);
    // log_message("Wrote update config in %s\n",config_file);
  }
  else
  {
    log_message("Could not write to config file located in: %s\n", config_file);
  }

  return 1;
}

/*
    trying to find where we are (where is openxr_loader.dll?)

    it will often not match the "current folder" of the process.

    in unity it is game_folder\game_data\plugins\x86_x64

    the plan is to look in the loaded module list, and pick
    the folder we are to use as a root folder.
    (search path for configuration and OG dll)

*/
int find_module_folder(char *search, char *output)
{
  HMODULE hMods[1024];
  HANDLE hProcess;
  DWORD cbNeeded;
  unsigned int i;
  DWORD processID = GetCurrentProcessId();

  // Print the process identifier.

  int ret_value = 0;
  // printf( "\nProcess ID: %u\n", processID );

  // Get a handle to the process.

  hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
                             PROCESS_VM_READ,
                         FALSE, processID);
  if (NULL == hProcess)
    return 1;

  // Get a list of all the modules in this process.

  if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
  {
    for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
    {
      TCHAR szModName[MAX_PATH];

      // Get the full path to the module's file.

      if (GetModuleFileNameExA(hProcess, hMods[i], szModName,
                               sizeof(szModName) / sizeof(TCHAR)))
      {
        // Print the module name and handle value.
        // printf( TEXT("\t%s (0x%08X)\n"), szModName, hMods[i] );

        // compare everything in lowercase
        str_lowercase(szModName);
        // find the last slash
        int s = -1;
        for (int c = 0; szModName[c] != 0; c++)
          if (szModName[c] == '\\')
            s = c;
        if (s > 0)
        {
          // the name of the file is the one we are looking for?
          if (strcmp(&szModName[s + 1], search) == 0)
          {
            // return the folder it is
            strncpy(output, szModName, s + 1);
            output[s + 1] = 0;
            ret_value = 1;
          }
        }
      }
    }
  }

  // Release the handle to the process.

  CloseHandle(hProcess);

  return ret_value;
}

static int join_path_filename(char *out, size_t out_sz, const char *folder, const char *filename)
{
  size_t folder_len;
  size_t filename_len;

  if (!out || !folder || !filename || out_sz == 0)
    return 0;

  folder_len = strlen(folder);
  filename_len = strlen(filename);

  if ((folder_len + filename_len + 1) > out_sz)
  {
    out[0] = 0;
    return 0;
  }

  memcpy(out, folder, folder_len);
  memcpy(out + folder_len, filename, filename_len);
  out[folder_len + filename_len] = 0;
  return 1;
}

static int find_action_binding_remap(const char *action_name, const char *source_path,
                                    int *mode, const char **target_path)
{
  for (int c = 0; c < action_binding_remap_count; c++)
  {
    ActionBindingRemap *remap = &action_binding_remaps[c];
    if (_stricmp(action_name, remap->action_name) == 0 &&
        _stricmp(source_path, remap->source_path) == 0)
    {
      *mode = remap->mode;
      *target_path = remap->target_path;
      return 1;
    }
  }
  return 0;
}

int log_init()
{
#ifdef DEBUG
  return 1;
#endif // DEBUG

  do
  {
    char log_filename[256];
    srand(time(NULL));
    snprintf(log_filename, sizeof(log_filename), "log_%d.txt", rand());
    h_log = fopen(log_filename, "a");
  } while (h_log == 0);

  log_message("--- log start ---");
  return 1;
}

int log_close()
{
  if (h_log)
  {
    fclose(h_log);
    h_log = 0;
  }
  return 1;
}

/*
    this hash function only has to be
    good enough to avoid colisions with
    the 55 ish names of functions in here

    i.e: i know its shit
*/
int simple_hash(const char *string)
{
  unsigned int hash = 0;
  for (int c = 0; string[c] != 0; c++)
  {
    // hash += string[c];
    hash = ((hash << 5) + hash) + string[c];
  }
  hash = hash % jump_table_size;
  return hash;
}

/*
    adds a name to the jump table

    the jump table exists to avoid a lot
    of strcmp calls when looking for
    a function name match.
*/
int assign_jump(const char *name, PFN_xrVoidFunction ptr)
{

  int position = simple_hash(name);

  if (jump_table[position] != 0)
  {
    char error[256];
    snprintf(error, sizeof(error), "Hash colision! hash %d - %s",
             position,
             name);
    printf("%s", error);

    exit(666);
  }
  else
  {
    jump_table[position] = ptr;
    char *n = &jump_table_names[position * jump_table_name_size];
    strncpy(n, name, jump_table_name_size - 1);
    n[jump_table_name_size - 1] = 0;
    // log_message("added %s to jumptable location %d",name,position);
  }
  return 1;
}

int search_jump(const char *name, PFN_xrVoidFunction *ptr)
{
  /*
      1) do hash
      2) look at "hash" position in list if there is a value there
      3) check if the "name" being looked for matches with the found entry
  */
  int p = simple_hash(name);
  char *n = &jump_table_names[p * jump_table_name_size];
  if (strcmp(name, n) == 0)
  {
    *ptr = jump_table[p];
    return 1;
  }
  else
  {
    return 0; // jump not in the list
  }
}

/*
    jump locations of each proxied function, functions with an preceding
    underline are the real dll ones.
*/

// those 3 functions are not in the exports, but exist in the OG dll? odd.
// probably many others fit this.
PFN_xrCreateHandTrackerEXT _xrCreateHandTrackerEXT = 0;
PFN_xrDestroyHandTrackerEXT _xrDestroyHandTrackerEXT = 0;
PFN_xrLocateHandJointsEXT _xrLocateHandJointsEXT = 0;

// those others are all exported by the OG dll. the ordinals match in the distributed binary.
PFN_xrGetInstanceProcAddr _xrGetInstanceProcAddr;
PFN_xrEnumerateApiLayerProperties _xrEnumerateApiLayerProperties;
PFN_xrEnumerateInstanceExtensionProperties _xrEnumerateInstanceExtensionProperties;
PFN_xrCreateInstance _xrCreateInstance;
PFN_xrDestroyInstance _xrDestroyInstance;
PFN_xrGetInstanceProperties _xrGetInstanceProperties;
PFN_xrPollEvent _xrPollEvent;
PFN_xrResultToString _xrResultToString;
PFN_xrStructureTypeToString _xrStructureTypeToString;
PFN_xrGetSystem _xrGetSystem;
PFN_xrGetSystemProperties _xrGetSystemProperties;
PFN_xrEnumerateEnvironmentBlendModes _xrEnumerateEnvironmentBlendModes;
PFN_xrCreateSession _xrCreateSession;
PFN_xrDestroySession _xrDestroySession;
PFN_xrEnumerateReferenceSpaces _xrEnumerateReferenceSpaces;
PFN_xrCreateReferenceSpace _xrCreateReferenceSpace;
PFN_xrGetReferenceSpaceBoundsRect _xrGetReferenceSpaceBoundsRect;
PFN_xrCreateActionSpace _xrCreateActionSpace;
PFN_xrLocateSpace _xrLocateSpace;
PFN_xrDestroySpace _xrDestroySpace;
PFN_xrEnumerateViewConfigurations _xrEnumerateViewConfigurations;
PFN_xrGetViewConfigurationProperties _xrGetViewConfigurationProperties;
PFN_xrEnumerateViewConfigurationViews _xrEnumerateViewConfigurationViews;
PFN_xrEnumerateSwapchainFormats _xrEnumerateSwapchainFormats;
PFN_xrCreateSwapchain _xrCreateSwapchain;
PFN_xrDestroySwapchain _xrDestroySwapchain;
PFN_xrEnumerateSwapchainImages _xrEnumerateSwapchainImages;
PFN_xrAcquireSwapchainImage _xrAcquireSwapchainImage;
PFN_xrWaitSwapchainImage _xrWaitSwapchainImage;
PFN_xrReleaseSwapchainImage _xrReleaseSwapchainImage;
PFN_xrBeginSession _xrBeginSession;
PFN_xrEndSession _xrEndSession;
PFN_xrRequestExitSession _xrRequestExitSession;
PFN_xrWaitFrame _xrWaitFrame;
PFN_xrBeginFrame _xrBeginFrame;
PFN_xrEndFrame _xrEndFrame;
PFN_xrLocateViews _xrLocateViews;
PFN_xrStringToPath _xrStringToPath;
PFN_xrPathToString _xrPathToString;
PFN_xrCreateActionSet _xrCreateActionSet;
PFN_xrDestroyActionSet _xrDestroyActionSet;
PFN_xrCreateAction _xrCreateAction;
PFN_xrDestroyAction _xrDestroyAction;
PFN_xrSuggestInteractionProfileBindings _xrSuggestInteractionProfileBindings;
PFN_xrAttachSessionActionSets _xrAttachSessionActionSets;
PFN_xrGetCurrentInteractionProfile _xrGetCurrentInteractionProfile;
PFN_xrGetActionStateBoolean _xrGetActionStateBoolean;
PFN_xrGetActionStateFloat _xrGetActionStateFloat;
PFN_xrGetActionStateVector2f _xrGetActionStateVector2f;
PFN_xrGetActionStatePose _xrGetActionStatePose;
PFN_xrSyncActions _xrSyncActions;
PFN_xrEnumerateBoundSourcesForAction _xrEnumerateBoundSourcesForAction;
PFN_xrGetInputSourceLocalizedName _xrGetInputSourceLocalizedName;
PFN_xrApplyHapticFeedback _xrApplyHapticFeedback;
PFN_xrStopHapticFeedback _xrStopHapticFeedback;

static const char *action_name_for_handle(XrAction action)
{
  for (int c = 0; c < action_metadata_cnt; c++)
  {
    if (action_metadata[c] == action)
      return action_metadata_names[c];
  }
  return core_action_name(action);
}

static XrAction action_handle_for_name(const char *name)
{
  for (int c = 0; c < action_metadata_cnt; c++)
  {
    if (_stricmp(action_metadata_names[c], name) == 0)
      return action_metadata[c];
  }
  return XR_NULL_HANDLE;
}

static int binding_profile_is_active(void)
{
  return binding_profile_process[0] != 0 &&
         _stricmp(process_executable_basename, binding_profile_process) == 0;
}

static void binding_path_to_string(XrInstance instance, XrPath path, char *buffer, size_t buffer_size)
{
  uint32_t count = 0;
  if (!buffer || buffer_size == 0)
    return;

  buffer[0] = 0;
  if (!_xrPathToString ||
      _xrPathToString(instance, path, (uint32_t)buffer_size, &count, buffer) != XR_SUCCESS)
    snprintf(buffer, buffer_size, "<path:%llu>", (unsigned long long)path);
}

static int find_binding_remap(const char *source_path, const char **target_path)
{
  for (int c = 0; c < binding_remap_count; c++)
  {
    if (_stricmp(source_path, binding_remaps[c].source_path) == 0)
    {
      *target_path = binding_remaps[c].target_path;
      return 1;
    }
  }
  return 0;
}

static void log_matched_action_space(XrAction action, XrPath subaction_path, XrSpace space, int is_left)
{
  char path_string[XR_MAX_PATH_LENGTH] = {0};

  if (subaction_path == id_user_hand_left)
    snprintf(path_string, sizeof(path_string), "/user/hand/left");
  else if (subaction_path == id_user_hand_right)
    snprintf(path_string, sizeof(path_string), "/user/hand/right");
  else
    snprintf(path_string, sizeof(path_string), "<path:%llu>", (unsigned long long)subaction_path);

  log_message("MATCHED ActionSpace space:%llu action:%s subaction_path:%s classification:%s frame:%s "
              "rotation_deg:%f,%f,%f position:%f,%f,%f",
              (unsigned long long)space,
              action_name_for_handle(action),
              path_string,
              is_left ? "left" : "right",
              is_left ? "unchanged" : (right_hand_frame_local ? "local" : "world"),
              radToDeg(is_left ? offset_rotation_left_x : offset_rotation_right_x),
              radToDeg(is_left ? offset_rotation_left_y : offset_rotation_right_y),
              radToDeg(is_left ? offset_rotation_left_z : offset_rotation_right_z),
              is_left ? offset_position_left_x : offset_position_right_x,
              is_left ? offset_position_left_y : offset_position_right_y,
              is_left ? offset_position_left_z : offset_position_right_z);
}

/*
    entry point of a windows dll.
*/
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
  if (reason == DLL_PROCESS_ATTACH)
  {
    hLThis = hInst;
    InitializeCriticalSection(&pose_snapshot_lock);

    // start a logfile (before any attempt of log_message!)
    log_init();

    char process_exe_name[MAX_PATH] = {0};
    DWORD process_exe_name_length = GetModuleFileNameA(NULL, process_exe_name, sizeof(process_exe_name));
    const char *process_exe_basename = process_exe_name;
    for (DWORD c = 0; c < process_exe_name_length; c++)
    {
      if (process_exe_name[c] == '\\' || process_exe_name[c] == '/')
        process_exe_basename = &process_exe_name[c + 1];
    }
    strncpy(process_executable_basename, process_exe_basename,
            sizeof(process_executable_basename) - 1);
    process_executable_basename[sizeof(process_executable_basename) - 1] = 0;
    log_message("process executable: %s", process_executable_basename[0] ? process_executable_basename : "<unknown>");

    // try to find myself (openxr_loader.dll) within the loaded modules
    // of the current process, we need that to know what folder to look
    // for other stuff. (config file and OG dll)
    if (!find_module_folder("openxr_loader.dll", root_folder))
    {
      root_folder[0] = 0; // not found? trying with the current directory anyway.
      log_message("could not find my own dll!");
    }
    else
    {
      log_message("found my own dll at %s", root_folder);
    }

    char ogdll_file_path[256];
    if (!join_path_filename(config_file_path, sizeof(config_file_path), root_folder, "openxr_loader_config.txt") ||
        !join_path_filename(ogdll_file_path, sizeof(ogdll_file_path), root_folder, "original_openxr_loader.dll"))
    {
      log_message("Path too long while building config/original loader filenames.");
      exit(0);
    }

    // MessageBoxA(0,config_file_path,"OpenXR controler offset mod",MB_ICONERROR);

    read_config(config_file_path);

    log_message("selected profile: legacy-flat");
    log_message("config path: %s", config_file_path);
    log_message("frame mode: %s", right_hand_frame_local ? "local" : "world");

    log_message("Using the following rotation offsets:");
    log_message("left: %fx%fx%f",
                radToDeg(offset_rotation_left_x),
                radToDeg(offset_rotation_left_y),
                radToDeg(offset_rotation_left_z));
    log_message("right: %fx%fx%f",
                radToDeg(offset_rotation_right_x),
                radToDeg(offset_rotation_right_y),
                radToDeg(offset_rotation_right_z));
    log_message("");

    // jumptable memory
    jump_table = calloc(jump_table_size, sizeof(*jump_table));
    jump_table_names = calloc(jump_table_size, jump_table_name_size);

    hL = LoadLibrary(ogdll_file_path);
    if (!hL)
    {
      char err[512];
      snprintf(err, sizeof(err), "Could not find:\n%s\n\nread the instructions for further information.\n\n(do not overwrite the original file! rename it!)", ogdll_file_path);
      MessageBoxA(0, err, "OpenXR controler offset mod", MB_ICONERROR);
      log_close();
      exit(0);
    }
    else
    {
      log_message("hook sucessful to original_openxr_loader.dll");
    }

    /*
    //those functions arent exported, but exist, oddly xrGetInstanceProcAddr can tell their address.
    _xrCreateHandTrackerEXT = (PFN_xrCreateHandTrackerEXT) GetProcAddress(hL,"xrCreateHandTrackerEXT");
    _xrDestroyHandTrackerEXT = (PFN_xrDestroyHandTrackerEXT) GetProcAddress(hL,"xrDestroyHandTrackerEXT");
    _xrLocateHandJointsEXT = (PFN_xrLocateHandJointsEXT) GetProcAddress(hL,"xrLocateHandJointsEXT");
    */

    /*
        those are pointers to the real dll functions, from exports

        the functions with a preceding underline are the real dll functions.

        the ones without are the proxied functions (ours)
    */
    _xrAcquireSwapchainImage = (PFN_xrAcquireSwapchainImage)GetProcAddress(hL, "xrAcquireSwapchainImage");
    _xrApplyHapticFeedback = (PFN_xrApplyHapticFeedback)GetProcAddress(hL, "xrApplyHapticFeedback");
    _xrAttachSessionActionSets = (PFN_xrAttachSessionActionSets)GetProcAddress(hL, "xrAttachSessionActionSets");
    _xrBeginFrame = (PFN_xrBeginFrame)GetProcAddress(hL, "xrBeginFrame");
    _xrBeginSession = (PFN_xrBeginSession)GetProcAddress(hL, "xrBeginSession");
    _xrCreateAction = (PFN_xrCreateAction)GetProcAddress(hL, "xrCreateAction");
    _xrCreateActionSet = (PFN_xrCreateActionSet)GetProcAddress(hL, "xrCreateActionSet");
    _xrCreateActionSpace = (PFN_xrCreateActionSpace)GetProcAddress(hL, "xrCreateActionSpace");
    _xrCreateInstance = (PFN_xrCreateInstance)GetProcAddress(hL, "xrCreateInstance");
    _xrCreateReferenceSpace = (PFN_xrCreateReferenceSpace)GetProcAddress(hL, "xrCreateReferenceSpace");
    _xrCreateSession = (PFN_xrCreateSession)GetProcAddress(hL, "xrCreateSession");
    _xrCreateSwapchain = (PFN_xrCreateSwapchain)GetProcAddress(hL, "xrCreateSwapchain");
    _xrDestroyAction = (PFN_xrDestroyAction)GetProcAddress(hL, "xrDestroyAction");
    _xrDestroyActionSet = (PFN_xrDestroyActionSet)GetProcAddress(hL, "xrDestroyActionSet");
    _xrDestroyInstance = (PFN_xrDestroyInstance)GetProcAddress(hL, "xrDestroyInstance");
    _xrDestroySession = (PFN_xrDestroySession)GetProcAddress(hL, "xrDestroySession");
    _xrDestroySpace = (PFN_xrDestroySpace)GetProcAddress(hL, "xrDestroySpace");
    _xrDestroySwapchain = (PFN_xrDestroySwapchain)GetProcAddress(hL, "xrDestroySwapchain");
    _xrEndFrame = (PFN_xrEndFrame)GetProcAddress(hL, "xrEndFrame");
    _xrEndSession = (PFN_xrEndSession)GetProcAddress(hL, "xrEndSession");
    _xrEnumerateApiLayerProperties = (PFN_xrEnumerateApiLayerProperties)GetProcAddress(hL, "xrEnumerateApiLayerProperties");
    _xrEnumerateBoundSourcesForAction = (PFN_xrEnumerateBoundSourcesForAction)GetProcAddress(hL, "xrEnumerateBoundSourcesForAction");
    _xrEnumerateEnvironmentBlendModes = (PFN_xrEnumerateEnvironmentBlendModes)GetProcAddress(hL, "xrEnumerateEnvironmentBlendModes");
    _xrEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties)GetProcAddress(hL, "xrEnumerateInstanceExtensionProperties");
    _xrEnumerateReferenceSpaces = (PFN_xrEnumerateReferenceSpaces)GetProcAddress(hL, "xrEnumerateReferenceSpaces");
    _xrEnumerateSwapchainFormats = (PFN_xrEnumerateSwapchainFormats)GetProcAddress(hL, "xrEnumerateSwapchainFormats");
    _xrEnumerateSwapchainImages = (PFN_xrEnumerateSwapchainImages)GetProcAddress(hL, "xrEnumerateSwapchainImages");
    _xrEnumerateViewConfigurationViews = (PFN_xrEnumerateViewConfigurationViews)GetProcAddress(hL, "xrEnumerateViewConfigurationViews");
    _xrEnumerateViewConfigurations = (PFN_xrEnumerateViewConfigurations)GetProcAddress(hL, "xrEnumerateViewConfigurations");
    _xrGetActionStateBoolean = (PFN_xrGetActionStateBoolean)GetProcAddress(hL, "xrGetActionStateBoolean");
    _xrGetActionStateFloat = (PFN_xrGetActionStateFloat)GetProcAddress(hL, "xrGetActionStateFloat");
    _xrGetActionStatePose = (PFN_xrGetActionStatePose)GetProcAddress(hL, "xrGetActionStatePose");
    _xrGetActionStateVector2f = (PFN_xrGetActionStateVector2f)GetProcAddress(hL, "xrGetActionStateVector2f");
    _xrGetCurrentInteractionProfile = (PFN_xrGetCurrentInteractionProfile)GetProcAddress(hL, "xrGetCurrentInteractionProfile");
    _xrGetInputSourceLocalizedName = (PFN_xrGetInputSourceLocalizedName)GetProcAddress(hL, "xrGetInputSourceLocalizedName");
    _xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)GetProcAddress(hL, "xrGetInstanceProcAddr");
    _xrGetInstanceProperties = (PFN_xrGetInstanceProperties)GetProcAddress(hL, "xrGetInstanceProperties");
    _xrGetReferenceSpaceBoundsRect = (PFN_xrGetReferenceSpaceBoundsRect)GetProcAddress(hL, "xrGetReferenceSpaceBoundsRect");
    _xrGetSystem = (PFN_xrGetSystem)GetProcAddress(hL, "xrGetSystem");
    _xrGetSystemProperties = (PFN_xrGetSystemProperties)GetProcAddress(hL, "xrGetSystemProperties");
    _xrGetViewConfigurationProperties = (PFN_xrGetViewConfigurationProperties)GetProcAddress(hL, "xrGetViewConfigurationProperties");
    _xrLocateSpace = (PFN_xrLocateSpace)GetProcAddress(hL, "xrLocateSpace");
    _xrLocateViews = (PFN_xrLocateViews)GetProcAddress(hL, "xrLocateViews");
    _xrPathToString = (PFN_xrPathToString)GetProcAddress(hL, "xrPathToString");
    _xrPollEvent = (PFN_xrPollEvent)GetProcAddress(hL, "xrPollEvent");
    _xrReleaseSwapchainImage = (PFN_xrReleaseSwapchainImage)GetProcAddress(hL, "xrReleaseSwapchainImage");
    _xrRequestExitSession = (PFN_xrRequestExitSession)GetProcAddress(hL, "xrRequestExitSession");
    _xrResultToString = (PFN_xrResultToString)GetProcAddress(hL, "xrResultToString");
    _xrStopHapticFeedback = (PFN_xrStopHapticFeedback)GetProcAddress(hL, "xrStopHapticFeedback");
    _xrStringToPath = (PFN_xrStringToPath)GetProcAddress(hL, "xrStringToPath");
    _xrStructureTypeToString = (PFN_xrStructureTypeToString)GetProcAddress(hL, "xrStructureTypeToString");
    _xrSuggestInteractionProfileBindings = (PFN_xrSuggestInteractionProfileBindings)GetProcAddress(hL, "xrSuggestInteractionProfileBindings");
    _xrSyncActions = (PFN_xrSyncActions)GetProcAddress(hL, "xrSyncActions");
    _xrWaitFrame = (PFN_xrWaitFrame)GetProcAddress(hL, "xrWaitFrame");
    _xrWaitSwapchainImage = (PFN_xrWaitSwapchainImage)GetProcAddress(hL, "xrWaitSwapchainImage");

    // do the jump table based on the dead simple hash names of the functions
    if (!jump_table || !jump_table_names)
    {
      log_message("Could not allocate jump table memory.");
      exit(0);
    }

    /*
        add the functions to the jump table (pointers to the proxy functions)

        any ommited functions arent proxied, so you can add/ommit stuff as
        you need.

        warning: i did mistakes while the matching function prototypes, therefore
                 some of those could crash due to mistakes in pointers and whatnot.
                 what happened is: i realised i didnt needed to implement everything
                 because i just wanted controler offsets, so "beware" of everything
                 else.
    */

    /*
    assign_jump("xrAcquireSwapchainImage",(PFN_xrVoidFunction) xrAcquireSwapchainImage);
    assign_jump("xrApplyHapticFeedback",(PFN_xrVoidFunction) xrApplyHapticFeedback);
    assign_jump("xrAttachSessionActionSets",(PFN_xrVoidFunction) xrAttachSessionActionSets);
    assign_jump("xrBeginFrame",(PFN_xrVoidFunction) xrBeginFrame);
    assign_jump("xrBeginSession",(PFN_xrVoidFunction) xrBeginSession);
    assign_jump("xrCreateInstance",(PFN_xrVoidFunction) xrCreateInstance);
    assign_jump("xrCreateReferenceSpace",(PFN_xrVoidFunction) xrCreateReferenceSpace);
    assign_jump("xrCreateSession",(PFN_xrVoidFunction) xrCreateSession);
    assign_jump("xrCreateSwapchain",(PFN_xrVoidFunction) xrCreateSwapchain);
    assign_jump("xrDestroyAction",(PFN_xrVoidFunction) xrDestroyAction);
    assign_jump("xrDestroyActionSet",(PFN_xrVoidFunction) xrDestroyActionSet);
    assign_jump("xrDestroyInstance",(PFN_xrVoidFunction) xrDestroyInstance);
    assign_jump("xrDestroySession",(PFN_xrVoidFunction) xrDestroySession);
    assign_jump("xrDestroySpace",(PFN_xrVoidFunction) xrDestroySpace);
    assign_jump("xrDestroySwapchain",(PFN_xrVoidFunction) xrDestroySwapchain);
    assign_jump("xrEndFrame",(PFN_xrVoidFunction) xrEndFrame);
    assign_jump("xrEndSession",(PFN_xrVoidFunction) xrEndSession);
    assign_jump("xrEnumerateApiLayerProperties",(PFN_xrVoidFunction) xrEnumerateApiLayerProperties);
    assign_jump("xrEnumerateBoundSourcesForAction",(PFN_xrVoidFunction) xrEnumerateBoundSourcesForAction);
    assign_jump("xrEnumerateEnvironmentBlendModes",(PFN_xrVoidFunction) xrEnumerateEnvironmentBlendModes);
    assign_jump("xrEnumerateInstanceExtensionProperties",(PFN_xrVoidFunction) xrEnumerateInstanceExtensionProperties);
    assign_jump("xrEnumerateReferenceSpaces",(PFN_xrVoidFunction) xrEnumerateReferenceSpaces);
    assign_jump("xrEnumerateSwapchainFormats",(PFN_xrVoidFunction) xrEnumerateSwapchainFormats);
    assign_jump("xrEnumerateSwapchainImages",(PFN_xrVoidFunction) xrEnumerateSwapchainImages);
    assign_jump("xrEnumerateViewConfigurationViews",(PFN_xrVoidFunction) xrEnumerateViewConfigurationViews);
    assign_jump("xrEnumerateViewConfigurations",(PFN_xrVoidFunction) xrEnumerateViewConfigurations);
    assign_jump("xrGetActionStateBoolean",(PFN_xrVoidFunction) xrGetActionStateBoolean);
    assign_jump("xrGetActionStateFloat",(PFN_xrVoidFunction) xrGetActionStateFloat);
    assign_jump("xrGetActionStatePose",(PFN_xrVoidFunction) xrGetActionStatePose);
    assign_jump("xrGetActionStateVector2f",(PFN_xrVoidFunction) xrGetActionStateVector2f);
    assign_jump("xrGetCurrentInteractionProfile",(PFN_xrVoidFunction) xrGetCurrentInteractionProfile);
    assign_jump("xrGetInputSourceLocalizedName",(PFN_xrVoidFunction) xrGetInputSourceLocalizedName);
    assign_jump("xrGetInstanceProcAddr",(PFN_xrVoidFunction) xrGetInstanceProcAddr);
    assign_jump("xrGetInstanceProperties",(PFN_xrVoidFunction) xrGetInstanceProperties);
    assign_jump("xrGetReferenceSpaceBoundsRect",(PFN_xrVoidFunction) xrGetReferenceSpaceBoundsRect);
    assign_jump("xrGetSystem",(PFN_xrVoidFunction) xrGetSystem);
    assign_jump("xrGetSystemProperties",(PFN_xrVoidFunction) xrGetSystemProperties);
    assign_jump("xrGetViewConfigurationProperties",(PFN_xrVoidFunction) xrGetViewConfigurationProperties);
    */

    assign_jump("xrCreateAction", (PFN_xrVoidFunction)xrCreateAction);
    assign_jump("xrCreateActionSet", (PFN_xrVoidFunction)xrCreateActionSet);
    assign_jump("xrCreateActionSpace", (PFN_xrVoidFunction)xrCreateActionSpace);

    assign_jump("xrGetActionStateBoolean", (PFN_xrVoidFunction)xrGetActionStateBoolean);
    assign_jump("xrGetActionStateFloat", (PFN_xrVoidFunction)xrGetActionStateFloat);

    assign_jump("xrLocateSpace", (PFN_xrVoidFunction)xrLocateSpace);

    assign_jump("xrLocateViews", (PFN_xrVoidFunction)xrLocateViews);

    assign_jump("xrStringToPath", (PFN_xrVoidFunction)xrStringToPath);

    assign_jump("xrSuggestInteractionProfileBindings", (PFN_xrVoidFunction)xrSuggestInteractionProfileBindings);

    /*
    assign_jump("xrLocateViews",(PFN_xrVoidFunction) xrLocateViews);
    assign_jump("xrPathToString",(PFN_xrVoidFunction) xrPathToString);
    assign_jump("xrPollEvent",(PFN_xrVoidFunction) xrPollEvent);
    assign_jump("xrReleaseSwapchainImage",(PFN_xrVoidFunction) xrReleaseSwapchainImage);
    assign_jump("xrRequestExitSession",(PFN_xrVoidFunction) xrRequestExitSession);
    assign_jump("xrResultToString",(PFN_xrVoidFunction) xrResultToString);
    assign_jump("xrStopHapticFeedback",(PFN_xrVoidFunction) xrStopHapticFeedback);
    assign_jump("xrStructureTypeToString",(PFN_xrVoidFunction) xrStructureTypeToString);
    assign_jump("xrSuggestInteractionProfileBindings",(PFN_xrVoidFunction) xrSuggestInteractionProfileBindings);
    assign_jump("xrSyncActions",(PFN_xrVoidFunction) xrSyncActions);
    assign_jump("xrWaitFrame",(PFN_xrVoidFunction) xrWaitFrame);
    assign_jump("xrWaitSwapchainImage",(PFN_xrVoidFunction) xrWaitSwapchainImage);
    */

    // launch the gui thread

    thread_gui_data *gui_stuff = (thread_gui_data *)malloc(sizeof(thread_gui_data));
    strncpy(gui_stuff->config_fp, config_file_path, sizeof(gui_stuff->config_fp) - 1);
    gui_stuff->config_fp[sizeof(gui_stuff->config_fp) - 1] = 0;

    launch_gui_thread(gui_stuff);
  }

  if (reason == DLL_PROCESS_DETACH)
  {
    // Do not tear down loader state from DllMain. Unity can still have
    // OpenXR callbacks in flight while the process is unloading modules.
    // The operating system releases these resources during process exit.
  }

  return 1;
}

// xrGetInstanceProcAddr
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(
    XrInstance instance,
    const char *name,
    PFN_xrVoidFunction *function)
{

  /*
      some monkey business needs to happen here.

      this function is when the game asks openXR what is the pointer
      of the named function so it call it by itself.

      if we just forward the original DLL answer, the game will
      "bypass" the proxy dll and call the original function instead

      what we need is to answer with "our" function when needed.

      this serves as an dual purpose to select what functions to
      actually handle here, if any are desired to be fully ignored.

  */

  // we call the real function anyway (to avoid changing the normal flow)
  XrResult ret = _xrGetInstanceProcAddr(instance, name, function);

  // if the real function succeded, we spoof the answer.
  if (ret == XR_SUCCESS)
  {
    // finding the same procedure inside our proxy dll
    PFN_xrVoidFunction our_target = 0;
    if (search_jump(name, &our_target))
    {
      log_message("i: %u - Rerouting %s to the proxy. (%p -> %p)", instance, name, function, our_target);

      // we override the answer to the game engine
      *function = our_target;
    }
    else
    {
      // the function to override is not in the jump table?

      // but is one of those know to be in the OG dll without an export?
      /*
      if (strcmp("-xrCreateHandTrackerEXT",name)==0)
      {
          _xrCreateHandTrackerEXT=function;
          function = xrCreateHandTrackerEXT;
      } else
      if (strcmp("-xrLocateHandJointsEXT",name)==0)
      {
          _xrLocateHandJointsEXT=function;
          function = xrLocateHandJointsEXT;
      } else
      if (strcmp("-xrDestroyHandTrackerEXT",name)==0)
      {
          _xrDestroyHandTrackerEXT=function;
          function = xrDestroyHandTrackerEXT;
      }*/

      // keeping the original dll answer, we dont have a replacement function
      // log_message("i: %d - Function requested is not in the jump table %s, bypassing to OG DLL: %p",instance,name,function);
    }
  }
  else
  {
    log_message("i: %d - xrGetInstanceProcAddr: %s not found on the original dll? - %d", instance, name, ret);
  }

  // whatever is the return value, always pass it forward!
  return ret;
}

void XRAPI_PTR xrCreateHandTrackerEXT(XrSession session, const XrHandTrackerCreateInfoEXT *createInfo, XrHandTrackerEXT *handTracker)
{
  log_message("xrCreateHandTrackerEXT");
  _xrCreateHandTrackerEXT(session, createInfo, handTracker);
}

void XRAPI_PTR xrDestroyHandTrackerEXT(XrHandTrackerEXT handTracker)
{
  log_message("xrDestroyHandTrackerEXT");
  _xrDestroyHandTrackerEXT(handTracker);
}

void XRAPI_PTR xrLocateHandJointsEXT(XrHandTrackerEXT handTracker, const XrHandJointsLocateInfoEXT *locateInfo, XrHandJointLocationsEXT *locations)
{
  log_message("xrLocateHandJointsEXT");
  _xrLocateHandJointsEXT(handTracker, locateInfo, locations);
}

// xrBeginSession
XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(
    XrSession session,
    const XrSessionBeginInfo *beginInfo)
{
  log_message("xrBeginSession %d", rand() % 1000);

  return _xrBeginSession(session, beginInfo);
}

// xrAcquireSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageAcquireInfo *acquireInfo,
    uint32_t *index)
{

  log_message("xrAcquireSwapchainImage %d", rand() % 1000);

  return _xrAcquireSwapchainImage(swapchain, acquireInfo, index);
}

// xrApplyHapticFeedback
XRAPI_ATTR XrResult XRAPI_CALL xrApplyHapticFeedback(
    XrSession session,
    const XrHapticActionInfo *hapticActionInfo,
    const XrHapticBaseHeader *hapticFeedback)
{
  log_message("xrApplyHapticFeedback %d", rand() % 1000);

  return _xrApplyHapticFeedback(session, hapticActionInfo, hapticFeedback);
}

// xrAttachSessionActionSets
XRAPI_ATTR XrResult XRAPI_CALL xrAttachSessionActionSets(
    XrSession session,
    const XrSessionActionSetsAttachInfo *attachInfo)
{

  log_message("xrAttachSessionActionSets %d", rand() % 1000);

  XrResult attached = _xrAttachSessionActionSets(session, attachInfo);
  proxy_dump_action_table("startup");
  return attached;
}

// xrBeginFrame
XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(
    XrSession session,
    const XrFrameBeginInfo *frameBeginInfo)
{
  log_message("xrBeginFrame %d", rand() % 1000);

  return _xrBeginFrame(session, frameBeginInfo);
}

// xrCreateAction
XRAPI_ATTR XrResult XRAPI_CALL xrCreateAction(
    XrActionSet actionSet,
    const XrActionCreateInfo *createInfo,
    XrAction *action)
{

  XrResult ret = _xrCreateAction(actionSet, createInfo, action);

  if (ret == XR_SUCCESS)
  {
    int is_pose_action = 0;
    if (action_metadata_cnt < ACTION_SPACE_LIST_CAPACITY)
    {
      action_metadata[action_metadata_cnt] = *action;
      strncpy(action_metadata_names[action_metadata_cnt], createInfo->actionName,
              sizeof(action_metadata_names[action_metadata_cnt]) - 1);
      action_metadata_names[action_metadata_cnt][sizeof(action_metadata_names[action_metadata_cnt]) - 1] = 0;
      action_metadata_cnt++;
    }
    core_trigger_note_action(*action, createInfo->actionType, createInfo->actionName);

    log_message("xrCreateAction parentActionset: %lu type: %d actiontype: %d actionname: %s localizedactionname: %s output: %lu",
                actionSet,
                createInfo->type,
                createInfo->actionType,
                createInfo->actionName,
                createInfo->localizedActionName,
                *action);

    is_pose_action = (createInfo->actionType == XR_ACTION_TYPE_POSE_INPUT);

    // list subpaths? (i dunno even what that is exactly)
    for (int c = 0; c < createInfo->countSubactionPaths; c++)
    {
      log_message("Subpath: %u", createInfo->subactionPaths[c]);
    }

    // bellow is a hack for cases where the game does not use the /user/hand/left|right
    //"pathString" to describe a controller, but the "actionname" related to the controller
    // the hack itself is horrible and more like a test to see what happens then.

    // does this actionName contains "left-controller" in the name
    // TODO: match only stuff saying left-controller-*-pose ?
    if (strncmp(createInfo->actionName, "left-controller", 15) == 0)
    {
      log_message("NOTICE: kept %s as a reference for the left controller. (%u)", createInfo->actionName, *action);
      if (ref_action_space_list_hand_left_cnt < ACTION_SPACE_LIST_CAPACITY)
      {
        ref_action_space_list_hand_left[ref_action_space_list_hand_left_cnt] = *action;
        ref_action_space_list_hand_left_cnt++;
      }
    }
    else if (strncmp(createInfo->actionName, "right-controller", 16) == 0)
    {
      log_message("NOTICE: kept %s as a reference for the right controller. (%lu)", createInfo->actionName, *action);
      if (ref_action_space_list_hand_right_cnt < ACTION_SPACE_LIST_CAPACITY)
      {
        ref_action_space_list_hand_right[ref_action_space_list_hand_right_cnt] = *action;
        ref_action_space_list_hand_right_cnt++;
      }

      if (is_pose_action)
      {
        const char *name = createInfo->actionName;
        int is_aim_pose = strstr(name, "aim") != NULL;
        int is_grip_pose = strstr(name, "grip") != NULL;
        if (is_aim_pose)
          log_message("NOTICE: action %s tagged as RIGHT AIM pose candidate.", name);
        else if (is_grip_pose)
          log_message("NOTICE: action %s tagged as RIGHT GRIP pose candidate.", name);
        else
          log_message("NOTICE: action %s tagged as RIGHT OTHER pose candidate.", name);
      }
    }
  }

  return ret;
}

// xrCreateActionSet
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSet(
    XrInstance instance,
    const XrActionSetCreateInfo *createInfo,
    XrActionSet *actionSet)
{

  XrResult ret = _xrCreateActionSet(instance, createInfo, actionSet);

  if (ret == XR_SUCCESS)
  {
    log_message("xrCreateActionSet type: %d - %s - %s - output: %lu",
                createInfo->type,
                createInfo->actionSetName,
                createInfo->localizedActionSetName,
                *actionSet);
  }
  return ret;
}

// xrCreateActionSpace
XRAPI_ATTR XrResult XRAPI_CALL xrCreateActionSpace(
    XrSession session,
    const XrActionSpaceCreateInfo *createInfo,
    XrSpace *space)
{

  XrResult ret = _xrCreateActionSpace(session, createInfo, space);

  log_message("xrCreateActionSpace action:%u  subactionpath:%lu next:%u space out:%lu",
              createInfo->action,
              // createInfo->poseInActionSpace,//algo sobre coordenadas desse action space
              createInfo->subactionPath,
              createInfo->next,
              *space);

  //===================================
  // is the "thing" attached to the user left hand or right hand? put it in the list...
  if (createInfo->subactionPath == id_user_hand_left)
  {
    log_message("NOTICE: assigned ActionSpace %u to the left controller by xrStringToPath match.", createInfo->action);
    if (action_space_list_hand_left_cnt < ACTION_SPACE_LIST_CAPACITY)
    {
      action_space_list_hand_left[action_space_list_hand_left_cnt] = *space;
      action_space_list_hand_left_cnt++;
    }
    log_matched_action_space(createInfo->action, createInfo->subactionPath, *space, 1);
    return ret;
  }
  if (createInfo->subactionPath == id_user_hand_right)
  {
    int kind = 0;
    log_message("NOTICE: assigned ActionSpace %u to the right controller by xrStringToPath match.", createInfo->action);
    if (action_space_list_hand_right_cnt < ACTION_SPACE_LIST_CAPACITY)
    {
      const char *action_name = action_name_for_handle(createInfo->action);
      if (action_name)
      {
        if (strstr(action_name, "aim") != NULL)
          kind = 2;
        else if (strstr(action_name, "grip") != NULL)
          kind = 1;
      }
      action_space_list_hand_right[action_space_list_hand_right_cnt] = *space;
      action_space_list_hand_right_kind[action_space_list_hand_right_cnt] = kind;
      action_space_list_hand_right_cnt++;
    }

    {
      const char *action_name = action_name_for_handle(createInfo->action);
      if (action_name && strstr(action_name, "aim") != NULL &&
          action_space_list_hand_right_aim_cnt < ACTION_SPACE_LIST_CAPACITY)
      {
        action_space_list_hand_right_aim[action_space_list_hand_right_aim_cnt] = *space;
        action_space_list_hand_right_aim_cnt++;
        log_message("NOTICE: assigned ActionSpace %u to RIGHT AIM list by xrStringToPath match.", createInfo->action);
      }
    }

    log_matched_action_space(createInfo->action, createInfo->subactionPath, *space, 0);
    return ret;
  }

  //====================================
  // is this action related to something known "within" left|right-controller-* ?
  for (int c = 0; c < ref_action_space_list_hand_left_cnt; c++)
  {
    if (ref_action_space_list_hand_left[c] == createInfo->action)
    {
      log_message("NOTICE: assigned ActionSpace %u to the left controller by xrCreateAction match.");
      if (action_space_list_hand_left_cnt < ACTION_SPACE_LIST_CAPACITY)
      {
        action_space_list_hand_left[action_space_list_hand_left_cnt] = *space;
        action_space_list_hand_left_cnt++;
      }
        log_matched_action_space(createInfo->action, createInfo->subactionPath, *space, 1);
      return ret;
    }
  }
  for (int c = 0; c < ref_action_space_list_hand_right_cnt; c++)
  {
    if (ref_action_space_list_hand_right[c] == createInfo->action)
    {
      int kind = 0;
      log_message("NOTICE: assigned ActionSpace %u to the right controller by xrCreateAction match.");
      if (action_space_list_hand_right_cnt < ACTION_SPACE_LIST_CAPACITY)
      {
        const char *action_name = action_name_for_handle(createInfo->action);
        if (action_name)
        {
          if (strstr(action_name, "aim") != NULL)
            kind = 2;
          else if (strstr(action_name, "grip") != NULL)
            kind = 1;
        }
        action_space_list_hand_right[action_space_list_hand_right_cnt] = *space;
        action_space_list_hand_right_kind[action_space_list_hand_right_cnt] = kind;
        action_space_list_hand_right_cnt++;
      }
      {
        const char *action_name = action_name_for_handle(createInfo->action);
        if (action_name && strstr(action_name, "aim") != NULL &&
            action_space_list_hand_right_aim_cnt < ACTION_SPACE_LIST_CAPACITY)
        {
          action_space_list_hand_right_aim[action_space_list_hand_right_aim_cnt] = *space;
          action_space_list_hand_right_aim_cnt++;
          log_message("NOTICE: assigned ActionSpace %u to RIGHT AIM list by xrCreateAction match.", createInfo->action);
        }
      }
      log_matched_action_space(createInfo->action, createInfo->subactionPath, *space, 0);
      return ret;
    }
  }

  log_message("NOTICE: ActionSpace %u is not assigned to any offset hacks.", createInfo->action);

  return ret;
}

// xrCreateInstance
XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(
    const XrInstanceCreateInfo *createInfo,
    XrInstance *instance)
{
  /*
      Todo: aqui dentro o aplicativo tem uma string q diz
            o seu nome, pode ser charmoso de mostrar na gui
  */

  log_message("xrCreateInstance %d", rand() % 1000);
  {
    XrResult created = _xrCreateInstance(createInfo, instance);
    if (created == XR_SUCCESS && instance)
    {
      diag_instance = *instance;
      atexit(proxy_exit_summary);
    }
    return created;
  }
}

// xrCreateReferenceSpace
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(
    XrSession session,
    const XrReferenceSpaceCreateInfo *createInfo,
    XrSpace *space)
{
  log_message("xrCreateReferenceSpace %d", createInfo->type);

  return _xrCreateReferenceSpace(session, createInfo, space);
}

// xrCreateSession
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(
    XrInstance instance,
    const XrSessionCreateInfo *createInfo,
    XrSession *session)
{
  log_message(" #### xrCreateSession %d #### ", rand() % 1000);
  return _xrCreateSession(instance, createInfo, session);
}

// xrCreateSwapchain
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(
    XrSession session,
    const XrSwapchainCreateInfo *createInfo,
    XrSwapchain *swapchain)
{
  log_message("xrCreateSwapchain %d", rand() % 1000);
  return _xrCreateSwapchain(session, createInfo, swapchain);
}

// xrDestroyAction
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyAction(
    XrAction action)
{
  log_message("xrDestroyAction %d", rand() % 1000);
  return _xrDestroyAction(action);
}

// xrDestroyActionSet
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyActionSet(
    XrActionSet actionSet)
{
  log_message("xrDestroyActionSet %d", rand() % 1000);
  return _xrDestroyActionSet(actionSet);
}

// xrDestroyInstance
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(
    XrInstance instance)
{
  log_message("xrDestroyInstance %d", rand() % 1000);
  return _xrDestroyInstance(instance);
}

// xrDestroySession
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(
    XrSession session)
{
  log_message("xrDestroySession %d", rand() % 1000);
  return _xrDestroySession(session);
}

// xrDestroySpace
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(
    XrSpace space)
{
  log_message("xrDestroySpace %d", rand() % 1000);
  return _xrDestroySpace(space);
}

// xrDestroySwapchain
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(
    XrSwapchain swapchain)
{
  log_message("xrDestroySwapchain %d", rand() % 1000);
  return _xrDestroySwapchain(swapchain);
}

// xrEndFrame
XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(
    XrSession session,
    const XrFrameEndInfo *frameEndInfo)
{
  log_message("xrEndFrame %d", rand() % 1000);
  return _xrEndFrame(session, frameEndInfo);
}

// xrEndSession
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(
    XrSession session)
{
  log_message("xrEndSession %d", rand() % 1000);
  return _xrEndSession(session);
}

// xrEnumerateApiLayerProperties
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateApiLayerProperties(
    uint32_t propertyCapacityInput,
    uint32_t *propertyCountOutput,
    XrApiLayerProperties *properties)
{
  return _xrEnumerateApiLayerProperties(propertyCapacityInput, propertyCountOutput, properties);
  log_message("xrEnumerateApiLayerProperties - count: %d", *propertyCountOutput);
}

// xrEnumerateBoundSourcesForAction
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateBoundSourcesForAction(
    XrSession session,
    const XrBoundSourcesForActionEnumerateInfo *enumerateInfo,
    uint32_t sourceCapacityInput,
    uint32_t *sourceCountOutput,
    XrPath *sources)
{
  XrResult ret = _xrEnumerateBoundSourcesForAction(session, enumerateInfo, sourceCapacityInput, sourceCountOutput, sources);
  if (ret == XR_SUCCESS && enumerateInfo && sources && sourceCountOutput && diag_instance != XR_NULL_HANDLE)
  {
    for (uint32_t c = 0; c < *sourceCountOutput && c < sourceCapacityInput; c++)
    {
      char path[XR_MAX_PATH_LENGTH];
      binding_path_to_string(diag_instance, sources[c], path, sizeof(path));
      core_trigger_mark_source_confirmed(enumerateInfo->action, path);
    }
  }
  log_message("xrEnumerateBoundSourcesForAction action=%s count=%u", action_name_for_handle(enumerateInfo ? enumerateInfo->action : XR_NULL_HANDLE),
              sourceCountOutput ? *sourceCountOutput : 0);
  return ret;
}

// xrEnumerateEnvironmentBlendModes
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t environmentBlendModeCapacityInput,
    uint32_t *environmentBlendModeCountOutput,
    XrEnvironmentBlendMode *environmentBlendModes)
{
  log_message("xrEnumerateEnvironmentBlendModes %d", rand() % 1000);
  return _xrEnumerateEnvironmentBlendModes(instance, systemId, viewConfigurationType, environmentBlendModeCapacityInput, environmentBlendModeCountOutput, environmentBlendModes);
}

// xrEnumerateInstanceExtensionProperties
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateInstanceExtensionProperties(
    const char *layerName,
    uint32_t propertyCapacityInput,
    uint32_t *propertyCountOutput,
    XrExtensionProperties *properties)
{

  // observing data before calling the real dll, this is useful to peek
  // and modify the "question" to openXR
  log_message("xrEnumerateInstanceExtensionProperties name: %s Capacity: %u ptr: %p",
              layerName,
              propertyCapacityInput,
              properties);

  // the real call
  XrResult ret = _xrEnumerateInstanceExtensionProperties(layerName, propertyCapacityInput, propertyCountOutput, properties);

  // observing data after calling the real dll, this is useful to peek
  // and modify the "answer" to the game engine
  log_message("Count output: %u  %p - ret: %d",
              *propertyCountOutput,
              properties,
              ret);

  return ret;
}

// xrEnumerateReferenceSpaces
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateReferenceSpaces(
    XrSession session,
    uint32_t spaceCapacityInput,
    uint32_t *spaceCountOutput,
    XrReferenceSpaceType *spaces)
{
  log_message("xrEnumerateReferenceSpaces %d", rand() % 1000);
  return _xrEnumerateReferenceSpaces(session, spaceCapacityInput, spaceCountOutput, spaces);
}

// xrEnumerateSwapchainFormats
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(
    XrSession session,
    uint32_t formatCapacityInput,
    uint32_t *formatCountOutput,
    int64_t *formats)
{
  log_message("xrEnumerateSwapchainFormats %d", rand() % 1000);
  return _xrEnumerateSwapchainFormats(session, formatCapacityInput, formatCountOutput, formats);
}

// xrEnumerateSwapchainImages
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(
    XrSwapchain swapchain,
    uint32_t imageCapacityInput,
    uint32_t *imageCountOutput,
    XrSwapchainImageBaseHeader *images)
{
  log_message("xrEnumerateSwapchainImages %d", rand() % 1000);
  return _xrEnumerateSwapchainImages(swapchain, imageCapacityInput, imageCountOutput, images);
}

// xrEnumerateViewConfigurationViews
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    uint32_t viewCapacityInput,
    uint32_t *viewCountOutput,
    XrViewConfigurationView *views)
{
  log_message("xrEnumerateViewConfigurationViews %d", rand() % 1000);
  return _xrEnumerateViewConfigurationViews(instance, systemId, viewConfigurationType, viewCapacityInput, viewCountOutput, views);
}

// xrEnumerateViewConfigurations
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurations(
    XrInstance instance,
    XrSystemId systemId,
    uint32_t viewConfigurationTypeCapacityInput,
    uint32_t *viewConfigurationTypeCountOutput,
    XrViewConfigurationType *viewConfigurationTypes)
{
  log_message("xrEnumerateViewConfigurations %d", rand() % 1000);
  return _xrEnumerateViewConfigurations(instance, systemId, viewConfigurationTypeCapacityInput, viewConfigurationTypeCountOutput, viewConfigurationTypes);
}

static int proxy_trigger_match(XrAction action, XrPath sub, int want_bool, const char **method)
{
  if (core_trigger_sources_known())
  {
    int flags = core_trigger_hand_flags(action);
    if (!(flags & CORE_HAND_RIGHT))
      return 0;
    if (sub != id_user_hand_right && !(sub == XR_NULL_PATH && flags == CORE_HAND_RIGHT))
      return 0;
    *method = "source_path";
    return 1;
  }
  if (sub != id_user_hand_right)
    return 0;
  if (_stricmp(action_name_for_handle(action), want_bool ? "triggerpressed" : "trigger") != 0)
    return 0;
  *method = "action_name_fallback";
  return 1;
}

static void proxy_dump_action_table(const char *why)
{
  char line[2048];
  log_message("ACTION_TABLE dump reason=%s count=%d", why, core_action_table_count());
  for (int c = 0; c < core_action_table_count(); c++)
  {
	if (core_action_table_format(c, line, sizeof(line)))
	  log_message("%s", line);
  }
}

static int proxy_click_match(XrAction action, XrPath sub)
{
  int flags = core_trigger_click_flags(action);
  if (!(flags & CORE_HAND_RIGHT))
	return 0;
  return sub == id_user_hand_right || (sub == XR_NULL_PATH && flags == CORE_HAND_RIGHT);
}

static void proxy_note_query(XrAction action, int modified, const char *kind)
{
  int r = core_query_note(action, modified);
  if (r & CORE_QUERY_FIRST_CLICK)
	log_message("*** GAME QUERIES TRIGGER CLICK ACTION: action=\"%s\" source=%s (%s) modified=%d ***",
				action_name_for_handle(action), core_click_source_path(action), kind, modified);
  if (r & CORE_QUERY_FIRST_UNMODIFIED)
	log_message("*** GAME QUERIES TRIGGER-BOUND ACTION THE PROXY IS NOT MODIFYING: action=\"%s\" value_source=%s click_source=%s (%s) ***",
				action_name_for_handle(action), core_trigger_source_path(action), core_click_source_path(action), kind);
}

static void proxy_trigger_diag(const char *method, XrAction action, float raw, const CoreTriggerCfg *cfg)
{
  float curved = core_trigger_curve(raw, cfg);
  int pressed = core_trigger_pressed(raw, cfg);
  if (core_trigger_log_gate(raw, curved, pressed, (unsigned long long)GetTickCount64()))
  {
	char line[768];
	core_trigger_format_diag(line, sizeof(line), method, action_name_for_handle(action), action, "right",
							 raw, curved, core_trigger_output(raw, cfg), pressed, cfg);
	log_message("%s", line);
  }
}

// xrGetActionStateBoolean
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateBoolean(
	XrSession session,
	const XrActionStateGetInfo *getInfo,
	XrActionStateBoolean *state)
{
  XrResult ret = _xrGetActionStateBoolean(session, getInfo, state);
  const char *method = "";
  int runtime_value = (ret == XR_SUCCESS && state) ? (int)state->currentState : 0;
  int modified = 0;
  int is_click = 0;
  float compare = 0.0f;

  if (ret == XR_SUCCESS && getInfo && state && _xrGetActionStateFloat &&
	  proxy_trigger_match(getInfo->action, getInfo->subactionPath, 1, &method) &&
	  !core_trigger_target_excludes(getInfo->action))
  {
	XrAction trigger_action = core_trigger_sources_known()
								  ? core_trigger_find_float_action(CORE_HAND_RIGHT)
								  : action_handle_for_name("trigger");
	if (trigger_action != XR_NULL_HANDLE)
	{
	  XrActionStateGetInfo trigger_info = {XR_TYPE_ACTION_STATE_GET_INFO};
	  XrActionStateFloat trigger_state = {XR_TYPE_ACTION_STATE_FLOAT};
	  trigger_info.action = trigger_action;
	  trigger_info.subactionPath = getInfo->subactionPath;

	  if (_xrGetActionStateFloat(session, &trigger_info, &trigger_state) == XR_SUCCESS &&
		  trigger_state.isActive)
	  {
		CoreTriggerCfg cfg;
		proxy_trigger_cfg(&cfg);
		if (core_trigger_method_first_use(strcmp(method, "source_path") == 0))
		  log_message("TRIGGER identification method: %s", method);
		state->currentState = core_trigger_pressed(trigger_state.currentState, &cfg);
		state->changedSinceLastSync = trigger_state.changedSinceLastSync;
		state->lastChangeTime = trigger_state.lastChangeTime;
		proxy_trigger_diag(method, trigger_action, trigger_state.currentState, &cfg);
		modified = 1;
	  }
	}
  }
  else if (ret == XR_SUCCESS && getInfo && state && _xrGetActionStateFloat &&
		   proxy_click_match(getInfo->action, getInfo->subactionPath))
  {
	CoreTriggerCfg cfg;
	proxy_trigger_cfg(&cfg);
	if (core_trigger_click_enabled(&cfg))
	{
	  XrAction trigger_action = core_trigger_find_float_action(CORE_HAND_RIGHT);
	  if (trigger_action != XR_NULL_HANDLE)
	  {
		XrActionStateGetInfo trigger_info = {XR_TYPE_ACTION_STATE_GET_INFO};
		XrActionStateFloat trigger_state = {XR_TYPE_ACTION_STATE_FLOAT};
		trigger_info.action = trigger_action;
		trigger_info.subactionPath = getInfo->subactionPath;
		if (_xrGetActionStateFloat(session, &trigger_info, &trigger_state) == XR_SUCCESS &&
			trigger_state.isActive)
		{
		  float raw = trigger_state.currentState;
		  compare = core_trigger_click_compare(raw, &cfg);
		  state->currentState = core_trigger_click(raw, runtime_value, &cfg);
		  modified = 1;
		  is_click = 1;
		}
	  }
	}
  }

  if (ret == XR_SUCCESS && getInfo && state)
  {
	CoreTriggerCfg cfg;
	const char *bound = core_click_source_path(getInfo->action);
	proxy_trigger_cfg(&cfg);
	if (bound[0] == '<')
	  bound = core_trigger_source_path(getInfo->action);
	proxy_note_query(getInfo->action, modified, "boolean");
	log_message("BOOLDIG action=\"%s\" bound=\"%s\" runtime=%d out=%d mode=%s threshold=%.6f compare=%.6f compare_kind=%s",
				action_name_for_handle(getInfo->action), bound, runtime_value, (int)state->currentState,
				core_click_mode_name(cfg.click_mode), cfg.click_threshold, compare,
				is_click ? core_click_compare_name(cfg.click_compare) : "not-click");
  }
  return ret;
}

// xrGetActionStateFloat
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateFloat(
	XrSession session,
	const XrActionStateGetInfo *getInfo,
	XrActionStateFloat *state)
{
  XrResult ret = _xrGetActionStateFloat(session, getInfo, state);
  const char *method = "";
  float runtime_value = (ret == XR_SUCCESS && state) ? state->currentState : 0.0f;
  int modified = 0;
  if (ret == XR_SUCCESS && getInfo && state && state->isActive &&
	  proxy_trigger_match(getInfo->action, getInfo->subactionPath, 0, &method) &&
	  !core_trigger_target_excludes(getInfo->action))
  {
	CoreTriggerCfg cfg;
	float raw = state->currentState;
	proxy_trigger_cfg(&cfg);
	if (core_trigger_method_first_use(strcmp(method, "source_path") == 0))
	  log_message("TRIGGER identification method: %s", method);
	state->currentState = core_trigger_output(raw, &cfg);
	core_trigger_track(raw, state->currentState);
	proxy_trigger_diag(method, getInfo->action, raw, &cfg);
	modified = 1;
  }
  if (ret == XR_SUCCESS && getInfo && state)
	proxy_note_query(getInfo->action, modified, "float");
  log_message("xrGetActionStateFloat action=%s runtime=%.6f out=%.6f modified=%d result=%d",
			  getInfo ? action_name_for_handle(getInfo->action) : "<none>",
			  runtime_value,
			  state ? state->currentState : 0.0f,
			  modified,
			  ret);
  return ret;
}

// xrGetActionStatePose
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStatePose(
    XrSession session,
    const XrActionStateGetInfo *getInfo,
    XrActionStatePose *state)
{
  log_message("xrGetActionStatePose %d", rand() % 1000);
  return _xrGetActionStatePose(session, getInfo, state);
}

// xrGetActionStateVector2f
XRAPI_ATTR XrResult XRAPI_CALL xrGetActionStateVector2f(
    XrSession session,
    const XrActionStateGetInfo *getInfo,
    XrActionStateVector2f *state)
{
  log_message("xrGetActionStateVector2f %d", rand() % 1000);
  return _xrGetActionStateVector2f(session, getInfo, state);
}
// xrGetCurrentInteractionProfile
XRAPI_ATTR XrResult XRAPI_CALL xrGetCurrentInteractionProfile(
    XrSession session,
    XrPath topLevelUserPath,
    XrInteractionProfileState *interactionProfile)
{
  XrResult ret = _xrGetCurrentInteractionProfile(session, topLevelUserPath, interactionProfile);
  if (ret == XR_SUCCESS && interactionProfile && diag_instance != XR_NULL_HANDLE)
  {
    char top[XR_MAX_PATH_LENGTH];
    char profile[XR_MAX_PATH_LENGTH];
    binding_path_to_string(diag_instance, topLevelUserPath, top, sizeof(top));
    binding_path_to_string(diag_instance, interactionProfile->interactionProfile, profile, sizeof(profile));
    log_message("PROFILE_DIAG user_path=%s interaction_profile=%s", top, profile);
  }
  return ret;
}

// xrGetInputSourceLocalizedName
XRAPI_ATTR XrResult XRAPI_CALL xrGetInputSourceLocalizedName(
    XrSession session,
    const XrInputSourceLocalizedNameGetInfo *getInfo,
    uint32_t bufferCapacityInput,
    uint32_t *bufferCountOutput,
    char *buffer)
{
  log_message("xrGetInputSourceLocalizedName %d", rand() % 1000);
  return _xrGetInputSourceLocalizedName(session, getInfo, bufferCapacityInput, bufferCountOutput, buffer);
}

// xrGetInstanceProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProperties(
    XrInstance instance,
    XrInstanceProperties *instanceProperties)
{
  log_message("xrGetInstanceProperties %d", rand() % 1000);
  return _xrGetInstanceProperties(instance, instanceProperties);
}

// xrGetReferenceSpaceBoundsRect
XRAPI_ATTR XrResult XRAPI_CALL xrGetReferenceSpaceBoundsRect(
    XrSession session,
    XrReferenceSpaceType referenceSpaceType,
    XrExtent2Df *bounds)
{
  log_message("xrGetReferenceSpaceBoundsRect %d", rand() % 1000);
  return _xrGetReferenceSpaceBoundsRect(session, referenceSpaceType, bounds);
}

// xrGetSystem
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(
    XrInstance instance,
    const XrSystemGetInfo *getInfo,
    XrSystemId *systemId)
{
  log_message("xrGetSystem %d", rand() % 1000);
  return _xrGetSystem(instance, getInfo, systemId);
}

// xrGetSystemProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystemProperties(
    XrInstance instance,
    XrSystemId systemId,
    XrSystemProperties *properties)
{
  log_message("xrGetSystemProperties %d", rand() % 1000);
  return _xrGetSystemProperties(instance, systemId, properties);
}

// xrGetViewConfigurationProperties
XRAPI_ATTR XrResult XRAPI_CALL xrGetViewConfigurationProperties(
    XrInstance instance,
    XrSystemId systemId,
    XrViewConfigurationType viewConfigurationType,
    XrViewConfigurationProperties *configurationProperties)
{
  log_message("xrGetViewConfigurationProperties %d", rand() % 1000);
  return _xrGetViewConfigurationProperties(instance, systemId, viewConfigurationType, configurationProperties);
}

// xrLocateSpace
XRAPI_ATTR XrResult XRAPI_CALL xrLocateSpace(
    XrSpace space,
    XrSpace baseSpace,
    XrTime time,
    XrSpaceLocation *location)
{

  XrResult ret = _xrLocateSpace(space, baseSpace, time, location);

  // if its the type we want for rotation offsets
  if (location->type == XR_TYPE_SPACE_LOCATION)
  {

    // is this the position data of the left hand?
    for (int c = 0; c < action_space_list_hand_left_cnt; c++)
    {
      if (space == action_space_list_hand_left[c])
      {
        update_pose_display_snapshot(1, baseSpace, &location->pose);

        Quaternion hand;

        /* Library uses zyxw, openXR uses xyzw */
        hand.v[Z_AXIS] = location->pose.orientation.z;
        hand.v[Y_AXIS] = location->pose.orientation.y;
        hand.v[X_AXIS] = location->pose.orientation.x;
        hand.w = location->pose.orientation.w;

        // adding rotation offset
        Quaternion rotation;
        /* GUI/config mapping: _x is physical pitch, _y is yaw, and _z is roll. */
        float rotation_zyx[3];
        rotation_zyx[Z_AXIS] = offset_rotation_left_z;
        rotation_zyx[Y_AXIS] = offset_rotation_left_y;
        rotation_zyx[X_AXIS] = offset_rotation_left_x;
        Quaternion_fromEulerZYX(rotation_zyx, &rotation);
        Quaternion_multiply(&rotation, &hand, &hand);

        float pr_zyx[3];         // the desired position offset
        float rotated_pr_zyx[3]; // holds the "rotated" position offset
        pr_zyx[X_AXIS] = offset_position_left_x;
        pr_zyx[Y_AXIS] = offset_position_left_y;
        pr_zyx[Z_AXIS] = offset_position_left_z;
        Quaternion_rotate(&rotation, pr_zyx, rotated_pr_zyx); // do the rotation

        // apply position offset (it is catesian 3D stuff so just sum the dif)
        location->pose.position.x += rotated_pr_zyx[X_AXIS];
        location->pose.position.y += rotated_pr_zyx[Y_AXIS];
        location->pose.position.z += rotated_pr_zyx[Z_AXIS];

        /* returning to the xyzw of OpenXR*/
        location->pose.orientation.z = hand.v[Z_AXIS];
        location->pose.orientation.y = hand.v[Y_AXIS];
        location->pose.orientation.x = hand.v[X_AXIS];
        location->pose.orientation.w = hand.w;

        return ret;
      }
    }
    // is this the position data of the right hand?
    for (int c = 0; c < action_space_list_hand_right_cnt; c++)
    {
      if (space == action_space_list_hand_right[c])
      {
        {
          int kind = right_action_space_kind(space);
          if (kind == 2 || (kind == 1 && action_space_list_hand_right_aim_cnt == 0))
            update_pose_display_snapshot(0, baseSpace, &location->pose);
        }

        Quaternion hand;

        /* Library uses zyxw, openXR uses xyzw quaternions*/
        hand.v[Z_AXIS] = location->pose.orientation.z;
        hand.v[Y_AXIS] = location->pose.orientation.y;
        hand.v[X_AXIS] = location->pose.orientation.x;
        hand.w = location->pose.orientation.w;
        Quaternion original_hand = hand;

        // adding rotation offset
        Quaternion rotation;
        float rotation_zyx[3];
        rotation_zyx[Z_AXIS] = offset_rotation_right_z;
        rotation_zyx[Y_AXIS] = offset_rotation_right_y;
        rotation_zyx[X_AXIS] = offset_rotation_right_x;
        Quaternion_fromEulerZYX(rotation_zyx, &rotation); // quatertion with rotation offsets
        if (right_hand_frame_local)
          Quaternion_multiply(&hand, &rotation, &hand);
        else
          Quaternion_multiply(&rotation, &hand, &hand);     // rotating the "real angle" with the added offset

        float pr_zyx[3];         // the desired position offset
        float rotated_pr_zyx[3]; // holds the "rotated" position offset
        pr_zyx[X_AXIS] = offset_position_right_x;
        pr_zyx[Y_AXIS] = offset_position_right_y;
        pr_zyx[Z_AXIS] = offset_position_right_z;
        if (right_hand_frame_local)
          Quaternion_rotate(&original_hand, pr_zyx, rotated_pr_zyx);
        else
          Quaternion_rotate(&rotation, pr_zyx, rotated_pr_zyx); // do the rotation

        // apply position offset (it is catesian 3D stuff so just sum the dif)
        location->pose.position.x += rotated_pr_zyx[X_AXIS];
        location->pose.position.y += rotated_pr_zyx[Y_AXIS];
        location->pose.position.z += rotated_pr_zyx[Z_AXIS];

        /* returning to the xyzw of OpenXR quaternions*/
        location->pose.orientation.z = hand.v[Z_AXIS];
        location->pose.orientation.y = hand.v[Y_AXIS];
        location->pose.orientation.x = hand.v[X_AXIS];
        location->pose.orientation.w = hand.w;

        return ret;
      }
      // same
    }

    // log_message("@xrLocateSpace for something else: %lu",space);
  }

  // all other cases
  return ret;
}

// xrLocateViews
XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(
    XrSession session,
    const XrViewLocateInfo *viewLocateInfo,
    XrViewState *viewState,
    uint32_t viewCapacityInput,
    uint32_t *viewCountOutput,
    XrView *views)
{
  XrResult ret = _xrLocateViews(session, viewLocateInfo, viewState, viewCapacityInput, viewCountOutput, views);

  if (ret == XR_SUCCESS && viewLocateInfo && viewCountOutput && *viewCountOutput > 0 && views)
  {
    EnterCriticalSection(&pose_snapshot_lock);
    latest_head_pose = views[0].pose;
    latest_head_base_space = viewLocateInfo->space;
    latest_head_pose_valid = 1;
    LeaveCriticalSection(&pose_snapshot_lock);
  }

  log_message("xrLocateViews %d", rand() % 1000);
  return ret;
}

// xrPathToString
XRAPI_ATTR XrResult XRAPI_CALL xrPathToString(
    XrInstance instance,
    XrPath path,
    uint32_t bufferCapacityInput,
    uint32_t *bufferCountOutput,
    char *buffer)
{
  log_message("xrPathToString %d", rand() % 1000);
  return _xrPathToString(instance, path, bufferCapacityInput, bufferCountOutput, buffer);
}

// xrPollEvent
XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(
    XrInstance instance,
    XrEventDataBuffer *eventData)
{
  log_message("xrPollEvent %d", rand() % 1000);
  return _xrPollEvent(instance, eventData);
}

// xrReleaseSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageReleaseInfo *releaseInfo)
{
  log_message("xrReleaseSwapchainImage %d", rand() % 1000);
  return _xrReleaseSwapchainImage(swapchain, releaseInfo);
}

// xrRequestExitSession
XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(
    XrSession session)
{
  log_message("xrRequestExitSession %d", rand() % 1000);
  return _xrRequestExitSession(session);
}

// xrResultToString
XRAPI_ATTR XrResult XRAPI_CALL xrResultToString(
    XrInstance instance,
    XrResult value,
    char buffer[XR_MAX_RESULT_STRING_SIZE])
{
  log_message("xrResultToString %d", rand() % 1000);
  return _xrResultToString(instance, value, buffer);
}

// xrStopHapticFeedback
XRAPI_ATTR XrResult XRAPI_CALL xrStopHapticFeedback(
    XrSession session,
    const XrHapticActionInfo *hapticActionInfo)
{
  log_message("xrStopHapticFeedback %d", rand() % 1000);
  return _xrStopHapticFeedback(session, hapticActionInfo);
}

// xrStringToPath
XRAPI_ATTR XrResult XRAPI_CALL xrStringToPath(
    XrInstance instance,
    const char *pathString,
    XrPath *path)
{

  XrResult ret = _xrStringToPath(instance, pathString, path);

  log_message("xrStringToPath pathString: %s answer: %lu",
              pathString,
              *path);

  // store in a very dumb way the number equivalent of those
  if (strcmp(pathString, "/user/hand/left") == 0)
    id_user_hand_left = *path;
  if (strcmp(pathString, "/user/hand/right") == 0)
    id_user_hand_right = *path;

  return ret;
}

// xrStructureTypeToString
XRAPI_ATTR XrResult XRAPI_CALL xrStructureTypeToString(
    XrInstance instance,
    XrStructureType value,
    char buffer[XR_MAX_STRUCTURE_NAME_SIZE])
{
  log_message("xrStructureTypeToString %d", rand() % 1000);
  return _xrStructureTypeToString(instance, value, buffer);
}

// xrSuggestInteractionProfileBindings
XRAPI_ATTR XrResult XRAPI_CALL xrSuggestInteractionProfileBindings(
    XrInstance instance,
    const XrInteractionProfileSuggestedBinding *suggestedBindings)
{
  if (!suggestedBindings)
    return _xrSuggestInteractionProfileBindings(instance, suggestedBindings);

  char interaction_profile[XR_MAX_PATH_LENGTH];
  binding_path_to_string(instance, suggestedBindings->interactionProfile,
                         interaction_profile, sizeof(interaction_profile));

  XrInteractionProfileSuggestedBinding forwarded = *suggestedBindings;
  int profile_active = binding_profile_is_active();
  int rewrite_count = 0;
  uint32_t additional_bindings = 0;
  if (profile_active && binding_remapping_enabled)
  {
    for (int c = 0; c < action_binding_remap_count; c++)
    {
      if (action_binding_remaps[c].mode == 1)
        additional_bindings++;
    }
  }

  XrActionSuggestedBinding *rewritten = NULL;

  if (binding_logging_enabled || profile_active)
  {
    log_message("xrSuggestInteractionProfileBindings profile=%s count=%u process=%s remap_enabled=%d",
                interaction_profile, suggestedBindings->countSuggestedBindings,
                process_executable_basename, profile_active && binding_remapping_enabled);
  }

  if (profile_active && binding_remapping_enabled && suggestedBindings->countSuggestedBindings > 0)
  {
    rewritten = (XrActionSuggestedBinding *)malloc(
        sizeof(*rewritten) * (suggestedBindings->countSuggestedBindings + additional_bindings));
    if (rewritten)
    {
      memcpy(rewritten, suggestedBindings->suggestedBindings,
             sizeof(*rewritten) * suggestedBindings->countSuggestedBindings);
      forwarded.suggestedBindings = rewritten;
    }
    else
    {
      log_message("WARNING: could not allocate binding rewrite buffer; forwarding unchanged.");
    }
  }

  uint32_t forwarded_count = 0;
  for (uint32_t c = 0; c < suggestedBindings->countSuggestedBindings; c++)
  {
    char source_path[XR_MAX_PATH_LENGTH];
    const XrActionSuggestedBinding *binding = &suggestedBindings->suggestedBindings[c];
    binding_path_to_string(instance, binding->binding, source_path, sizeof(source_path));
    core_trigger_mark_source(binding->action, source_path);

    const char *replacement = NULL;
    int action_mode = 0;
    const char *action_replacement = NULL;
    const char *action_name = action_name_for_handle(binding->action);
    int action_configured = profile_active && binding_remapping_enabled &&
                            find_action_binding_remap(action_name, source_path,
                                                     &action_mode, &action_replacement);
    int configured = profile_active && binding_remapping_enabled && rewritten &&
                     find_binding_remap(source_path, &replacement);
    if (action_configured && action_mode == 2)
    {
      log_message("BINDING action=%s source=%s final=<disabled> rewrite=yes",
                  action_name, source_path);
      rewrite_count++;
      continue;
    }

    if (action_configured && action_mode == 0)
    {
      configured = 1;
      replacement = action_replacement;
    }

    if (rewritten)
      rewritten[forwarded_count] = *binding;
    if (configured)
    {
      XrPath replacement_path = XR_NULL_PATH;
      XrResult path_result = _xrStringToPath(instance, replacement, &replacement_path);
      if (path_result == XR_SUCCESS)
      {
        rewritten[forwarded_count].binding = replacement_path;
        rewrite_count++;
      }
      else
      {
        log_message("WARNING: remap action=%s source=%s replacement=%s could not resolve (%d).",
                    action_name_for_handle(binding->action), source_path, replacement, path_result);
      }
    }

    if (binding_logging_enabled || configured)
    {
      char final_path[XR_MAX_PATH_LENGTH];
      binding_path_to_string(instance, configured ? rewritten[forwarded_count].binding : binding->binding,
                             final_path, sizeof(final_path));
      log_message("BINDING action=%s handle=%llu source=%s final=%s rewrite=%s",
                  action_name_for_handle(binding->action),
                  (unsigned long long)binding->action, source_path, final_path,
                  configured ? "yes" : "no");
    }
    forwarded_count++;
  }

  if (rewritten && profile_active && binding_remapping_enabled)
  {
    for (int c = 0; c < action_binding_remap_count; c++)
    {
      ActionBindingRemap *remap = &action_binding_remaps[c];
      if (remap->mode != 1)
        continue;

      XrAction action = action_handle_for_name(remap->action_name);
      XrPath path = XR_NULL_PATH;
      if (action == XR_NULL_HANDLE || _xrStringToPath(instance, remap->target_path, &path) != XR_SUCCESS)
        continue;

      rewritten[forwarded_count].action = action;
      rewritten[forwarded_count].binding = path;
      log_message("BINDING action=%s source=<added> final=%s rewrite=yes",
                  remap->action_name, remap->target_path);
      forwarded_count++;
      rewrite_count++;
    }
  }

  if (rewritten)
    forwarded.countSuggestedBindings = forwarded_count;

  if (rewrite_count > 0)
    log_message("Binding rewrite applied: %d of %u bindings.", rewrite_count,
                suggestedBindings->countSuggestedBindings);

  XrResult result = _xrSuggestInteractionProfileBindings(instance, &forwarded);
  free(rewritten);
  return result;
}

// xrSyncActions
XRAPI_ATTR XrResult XRAPI_CALL xrSyncActions(
    XrSession session,
    const XrActionsSyncInfo *syncInfo)
{
  log_message("xrSyncActions %d", rand() % 1000);
  if (InterlockedExchange(&action_dump_requested, 0))
    proxy_dump_action_table("gui_request");
  return _xrSyncActions(session, syncInfo);
}

// xrWaitFrame
XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(
    XrSession session,
    const XrFrameWaitInfo *frameWaitInfo,
    XrFrameState *frameState)
{
  log_message("xrWaitFrame %d", rand() % 1000);
  return _xrWaitFrame(session, frameWaitInfo, frameState);
}

// xrWaitSwapchainImage
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(
    XrSwapchain swapchain,
    const XrSwapchainImageWaitInfo *waitInfo)
{
  log_message("xrWaitSwapchainImage %d", rand() % 1000);
  return _xrWaitSwapchainImage(swapchain, waitInfo);
}

/*
    =====================================================
    everything beyond this point is testing for some
    basic sanities because its been a while i havent
    programmed something with confusing pointers.

    this REALLY does not fit any purpose in the dll, but
    if you compile this as an console application, it
    will call the main function and do some basic
    stuff to see if the proxy is working properly. (ish)

    test code is partially borrowed from here:
    https://gitlab.freedesktop.org/monado/demos/openxr-simple-example/-/blob/master/main.c

    just enough to see if openXR gets pasts the first few calls.
*/

int xr_check(XrInstance instance, XrResult result, const char *format, ...)
{
  if (XR_SUCCEEDED(result))
    return 1;

  char resultString[XR_MAX_RESULT_STRING_SIZE];
  xrResultToString(instance, result, resultString);

  char formatRes[XR_MAX_RESULT_STRING_SIZE + 1024];
  snprintf(formatRes, XR_MAX_RESULT_STRING_SIZE + 1023, "%s [%s] (%d)\n", format, resultString,
           result);

  va_list args;
  va_start(args, format);
  vprintf(formatRes, args);
  va_end(args);

  return 0;
}

static void
print_viewconfig_view_info(uint32_t view_count, XrViewConfigurationView *viewconfig_views)
{
  for (uint32_t i = 0; i < view_count; i++)
  {
    printf("View Configuration View %d:\n", i);
    printf("\tResolution       : Recommended %dx%d, Max: %dx%d\n",
           viewconfig_views[0].recommendedImageRectWidth,
           viewconfig_views[0].recommendedImageRectHeight, viewconfig_views[0].maxImageRectWidth,
           viewconfig_views[0].maxImageRectHeight);
    printf("\tSwapchain Samples: Recommended: %d, Max: %d)\n",
           viewconfig_views[0].recommendedSwapchainSampleCount,
           viewconfig_views[0].maxSwapchainSampleCount);
  }
}

/*
    OpenXR internally has its quaternions in 32bit precision

    to avoid losing precision during calculations, we internally
    mess with them at 64bit precision, then downgrade back to 32bit.
*/
void quaternion_double_to_float(double input[4], double output[4])
{
  output[0] = input[0];
  output[1] = input[1];
  output[2] = input[2];
  output[3] = input[3];
}
void quaternion_float_to_double(float input[4], double output[4])
{
  output[0] = input[0];
  output[1] = input[1];
  output[2] = input[2];
  output[3] = input[3];
}

void print_euler(float e[3])
{
  printf("%8.3f x %8.3f x %8.3f",
         radToDeg(e[0]),
         radToDeg(e[1]),
         radToDeg(e[2]));
}

void print_quaternion(Quaternion q)
{
  float euler_zyx[3];
  Quaternion_toEulerZYX(&q, euler_zyx);
  printf("%8.3f x %8.3f x %8.3f",
         radToDeg(euler_zyx[0]),
         radToDeg(euler_zyx[1]),
         radToDeg(euler_zyx[2]));
}

void sprint_quaternion(char *s, Quaternion q)
{
  float euler_zyx[3];
  Quaternion_toEulerZYX(&q, euler_zyx);
  snprintf(s, 64, "%8.3f x %8.3f x %8.3f",
           radToDeg(euler_zyx[0]),
           radToDeg(euler_zyx[1]),
           radToDeg(euler_zyx[2]));
}

/*
    Keep in mind that this project is a DLL and the main function is not called!

    (unless you are trying something in a debug build as a console/gui application.)

    bellow its just a bunch of tests of stuff in the code without having to properly
    launch a game.

*/

int main()
{

  /*
  void print_xyz(float coords[3])
  {
      printf("%.4f x %.4f x %.4f\n",
             coords[X_AXIS],
             coords[Y_AXIS],
             coords[Z_AXIS]
             );
  }


  Quaternion rotation;
  float rotation_zyx[3];
  rotation_zyx[Z_AXIS] = degToRad(0.0);
  rotation_zyx[Y_AXIS] = degToRad(0.0);
  rotation_zyx[X_AXIS] = degToRad(0.0);
  Quaternion_fromEulerZYX(rotation_zyx,&rotation);


  //trying to produce XYZ rotation after offsets were added
  float pr_zyx[3];
  float rotated_pr_zyx[3];
  pr_zyx[X_AXIS] = 0.0;
  pr_zyx[Y_AXIS] = 10.0;
  pr_zyx[Z_AXIS] = 0.0;
  Quaternion_rotate(&rotation,pr_zyx,rotated_pr_zyx);


  print_xyz(pr_zyx);
  print_xyz(rotated_pr_zyx);


  exit(0);
  */

  launch_gui_thread(NULL);

  while (1)
  {
    Sleep(5);
  }

  /*
  char root_folder[256];
  find_module_folder("original_openxr_loader.dll",root_folder);

  printf("Found in: %s\n",root_folder);
  exit(0);
  */

  printf("debug\n");
  DllMain(0, DLL_PROCESS_ATTACH, 0);

  PFN_xrVoidFunction ptr = 0;
  if (search_jump("xrGetViewConfigurationProperties", &ptr))
  {
    printf("achei ptr %p\n", ptr);
  }
  else
  {
    printf("funcao nao inclusa! %p\n", ptr);
  }

  printf("-------------- %zu\n", sizeof(ptr));
  // ptr = 0;

  PFN_xrCreateInstance kek_xrCreateInstance = 0;
  PFN_xrVoidFunction generic_fp = NULL;
  xrGetInstanceProcAddr(0, "xrCreateInstance", &generic_fp);
  kek_xrCreateInstance = (PFN_xrCreateInstance)generic_fp;
  printf("Compare ptr: %p and %p\n", kek_xrCreateInstance, _xrCreateInstance);

  PFN_xrEnumerateInstanceExtensionProperties kek_xrEnumerateInstanceExtensionProperties = 0;
  generic_fp = NULL;
  xrGetInstanceProcAddr(0, "xrEnumerateInstanceExtensionProperties", &generic_fp);
  kek_xrEnumerateInstanceExtensionProperties = (PFN_xrEnumerateInstanceExtensionProperties)generic_fp;

  printf("Compare ptr: %p and %p\n", kek_xrEnumerateInstanceExtensionProperties, kek_xrEnumerateInstanceExtensionProperties);

  //=======================================
  // demo sanity test

  int enabled_ext_count = 0;
  // const char* enabled_exts[1] = {XR_KHR_OPENGL_ENABLE_EXTENSION_NAME};

  XrInstanceCreateInfo instance_create_info = {
      .type = XR_TYPE_INSTANCE_CREATE_INFO,
      .next = NULL,
      .createFlags = 0,
      .enabledExtensionCount = enabled_ext_count,
      //.enabledExtensionNames = enabled_exts,
      .enabledApiLayerCount = 0,
      .enabledApiLayerNames = NULL,
      .applicationInfo =
          {
              // some compilers have trouble with char* initialization
              .applicationName = "",
              .engineName = "",
              .applicationVersion = 1,
              .engineVersion = 0,
              .apiVersion = XR_CURRENT_API_VERSION,
          },
  };

  // Changing to HANDHELD_DISPLAY or a future form factor may work, but has not been tested.
  XrFormFactor form_factor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

  // Changing the form_factor may require changing the view_type too.
  XrViewConfigurationType view_type = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

  // Typically STAGE for room scale/standing, LOCAL for seated
  XrReferenceSpaceType play_space_type = XR_REFERENCE_SPACE_TYPE_LOCAL;
  XrSpace play_space = XR_NULL_HANDLE;

  // the system represents an (opaque) set of XR devices in use, managed by the runtime
  XrSystemId system_id = XR_NULL_SYSTEM_ID;
  // the session deals with the renderloop submitting frames to the runtime
  XrSession session = XR_NULL_HANDLE;
  XrInstance instance = XR_NULL_HANDLE;

  (void)play_space_type;
  (void)play_space;
  (void)session;

  strncpy(instance_create_info.applicationInfo.applicationName, "OpenXR OpenGL Example",
          XR_MAX_APPLICATION_NAME_SIZE);
  strncpy(instance_create_info.applicationInfo.engineName, "Custom", XR_MAX_ENGINE_NAME_SIZE);

  XrResult result = kek_xrCreateInstance(&instance_create_info, &instance);
  if (!xr_check(NULL, result, "Failed to create XR instance."))
    return 1;
  else
  {
    printf("Sucesso em kek_xrCreateInstance\n");
  }

  printf("------------\n");
  unsigned int cnt;
  XrExtensionProperties array_prop[64];

  for (int c = 0; c < 64; c++)
  {
    array_prop[c].type = XR_TYPE_EXTENSION_PROPERTIES;
    array_prop[c].next = &array_prop[c];
  }

  kek_xrEnumerateInstanceExtensionProperties(0, 0, &cnt, 0);
  unsigned int request_n = cnt;

  int ret = kek_xrEnumerateInstanceExtensionProperties(0, request_n, &cnt, array_prop);
  printf("retorno: %d\n", ret);

  for (int c = 0; c < cnt; c++)
  {
    printf("%s %u\n", array_prop[c].extensionName, array_prop[c].extensionVersion);
  }

  printf("xrEnumerateInstanceExtensionProperties: cnt %u\n", cnt);

  printf("------------\n");

  // --- Get XrSystemId
  XrSystemGetInfo system_get_info = {
      .type = XR_TYPE_SYSTEM_GET_INFO, .formFactor = form_factor, .next = NULL};

  result = xrGetSystem(instance, &system_get_info, &system_id);
  if (!xr_check(instance, result, "Failed to get system for HMD form factor."))
    return 1;

  printf("Successfully got XrSystem with id %llu for HMD form factor\n", (unsigned long long)system_id);

  uint32_t view_count = 0;
  // the viewconfiguration views contain information like resolution about each view
  XrViewConfigurationView *viewconfig_views = NULL;

  result = xrEnumerateViewConfigurationViews(instance, system_id, view_type, 0, &view_count, NULL);
  if (!xr_check(instance, result, "Failed to get view configuration view count!"))
    return 1;

  viewconfig_views = malloc(sizeof(XrViewConfigurationView) * view_count);
  for (uint32_t i = 0; i < view_count; i++)
  {
    viewconfig_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    viewconfig_views[i].next = NULL;
  }

  result = xrEnumerateViewConfigurationViews(instance, system_id, view_type, view_count,
                                             &view_count, viewconfig_views);
  if (!xr_check(instance, result, "Failed to enumerate view configuration views!"))
    return 1;
  print_viewconfig_view_info(view_count, viewconfig_views);

  // quaternion test
  XrSpaceLocation location;
  (void)location;

  printf("--------------\n");

  return 0;
}
