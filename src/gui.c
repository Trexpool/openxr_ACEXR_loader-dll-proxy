
#include "gui.h"
#include "xr_core.h"
#include <ctype.h>
#include <math.h>

HWND hwnd;        /* This is the handle for our window */
static HFONT gui_font = NULL;

static HFONT get_gui_font(void)
{
  if (!gui_font)
  {
    NONCLIENTMETRICSA ncm;
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
      gui_font = CreateFontIndirectA(&ncm.lfMessageFont);
    if (!gui_font)
      gui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  }
  return gui_font;
}

static void apply_gui_font(HANDLE control)
{
  if (control)
    SendMessage((HWND)control, WM_SETFONT, (WPARAM)get_gui_font(), MAKELPARAM(TRUE, 0));
}
MSG messages;     /* Here messages to the application are saved */
WNDCLASSEX wincl; /* Data structure for the windowclass */
HANDLE hInstance;

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

#define degToRad(angleInDegrees) ((angleInDegrees) * M_PI / 180.0)
#define radToDeg(angleInRadians) ((angleInRadians) * 180.0 / M_PI)

HANDLE HWND_LABEL_L_RX;
HANDLE HWND_LABEL_L_RY;
HANDLE HWND_LABEL_L_RZ;

HANDLE HWND_LABEL_L_PX;
HANDLE HWND_LABEL_L_PY;
HANDLE HWND_LABEL_L_PZ;
HANDLE HWND_LABEL_R_PX;
HANDLE HWND_LABEL_R_PY;
HANDLE HWND_LABEL_R_PZ;

HANDLE HWND_LABEL_R_RX;
HANDLE HWND_LABEL_R_RY;
HANDLE HWND_LABEL_R_RZ;

char config_fp[256];

int flag_quit = 0;

#define GUI_REPEAT_TIMER 1
#define GUI_POSE_TIMER 2
int repeat_button_id = 0;
HWND repeat_button_hwnd = NULL;
int suppress_button_click = 0;
static HANDLE gui_instance_mutex = NULL;
static HBRUSH editable_control_brush = NULL;
static COLORREF editable_control_color = RGB(224, 224, 224);

HANDLE HWND_POSE_L_RX;
HANDLE HWND_POSE_L_RY;
HANDLE HWND_POSE_L_RZ;
HANDLE HWND_POSE_L_PX;
HANDLE HWND_POSE_L_PY;
HANDLE HWND_POSE_L_PZ;
HANDLE HWND_POSE_R_RX;
HANDLE HWND_POSE_R_RY;
HANDLE HWND_POSE_R_RZ;
HANDLE HWND_POSE_R_PX;
HANDLE HWND_POSE_R_PY;
HANDLE HWND_POSE_R_PZ;

#define offset_dialog_constats 32000

enum
{
  ID_NULL,
  ID_LABEL_L_RY,
  ID_LABEL_L_RX,
  ID_LABEL_L_RZ,

  ID_LABEL_L_PX,
  ID_LABEL_L_PY,
  ID_LABEL_L_PZ,

  ID_LABEL_R_PX,
  ID_LABEL_R_PY,
  ID_LABEL_R_PZ,

  ID_BTN_L_YP_10,
  ID_BTN_L_YP_01,
  ID_BTN_L_YN_10,
  ID_BTN_L_YN_01,

  ID_BTN_L_XP_10,
  ID_BTN_L_XP_01,
  ID_BTN_L_XN_10,
  ID_BTN_L_XN_01,

  ID_BTN_L_ZP_10,
  ID_BTN_L_ZP_01,
  ID_BTN_L_ZN_10,
  ID_BTN_L_ZN_01,

  ID_BTN_L_P_XP_10,
  ID_BTN_L_P_XP_01,
  ID_BTN_L_P_XN_10,
  ID_BTN_L_P_XN_01,

  ID_BTN_L_P_YP_10,
  ID_BTN_L_P_YP_01,
  ID_BTN_L_P_YN_10,
  ID_BTN_L_P_YN_01,

  ID_BTN_L_P_ZP_10,
  ID_BTN_L_P_ZP_01,
  ID_BTN_L_P_ZN_10,
  ID_BTN_L_P_ZN_01,

  ID_BTN_R_P_XP_10,
  ID_BTN_R_P_XP_01,
  ID_BTN_R_P_XN_10,
  ID_BTN_R_P_XN_01,

  ID_BTN_R_P_YP_10,
  ID_BTN_R_P_YP_01,
  ID_BTN_R_P_YN_10,
  ID_BTN_R_P_YN_01,

  ID_BTN_R_P_ZP_10,
  ID_BTN_R_P_ZP_01,
  ID_BTN_R_P_ZN_10,
  ID_BTN_R_P_ZN_01,

  ID_LABEL_R_RY,
  ID_LABEL_R_RX,
  ID_LABEL_R_RZ,

  ID_BTN_R_YP_10,
  ID_BTN_R_YP_01,
  ID_BTN_R_YN_10,
  ID_BTN_R_YN_01,

  ID_BTN_R_XP_10,
  ID_BTN_R_XP_01,
  ID_BTN_R_XN_10,
  ID_BTN_R_XN_01,

  ID_BTN_R_ZP_10,
  ID_BTN_R_ZP_01,
  ID_BTN_R_ZN_10,
  ID_BTN_R_ZN_01,

  ID_POSE_L_RX,
  ID_POSE_L_RY,
  ID_POSE_L_RZ,
  ID_POSE_L_PX,
  ID_POSE_L_PY,
  ID_POSE_L_PZ,
  ID_POSE_R_RX,
  ID_POSE_R_RY,
  ID_POSE_R_RZ,
  ID_POSE_R_PX,
  ID_POSE_R_PY,
  ID_POSE_R_PZ,
  ID_BTN_CAPTURE_POSE,
  ID_BTN_TOGGLE_FRAME,
  ID_BTN_CAPTURE_MODE,
  ID_LABEL_POSE_STATUS,
  ID_LABEL_TRIGGER_THRESHOLD,
  ID_BTN_TRIGGER_THRESHOLD_DOWN,
  ID_BTN_TRIGGER_THRESHOLD_UP,
  ID_LABEL_TRIGGER_EXPONENT,
  ID_BTN_TRIGGER_EXPONENT_DOWN,
  ID_BTN_TRIGGER_EXPONENT_UP,
  ID_LABEL_TRIGGER_DEADZONE,
  ID_BTN_TRIGGER_DEADZONE_DOWN,
  ID_BTN_TRIGGER_DEADZONE_UP,
  ID_BTN_TRIGGER_RESET,
  ID_LABEL_TRIGGER_PREVIEW,
  ID_LABEL_CLICK,
  ID_BTN_CLICK_DOWN,
  ID_BTN_CLICK_UP,
  ID_BTN_CLICK_OFF,
  ID_BTN_DUMP_ACTIONS
};

static HANDLE add_readonly(int x, int y, int tx, int ty, char *text, int id);
static void update_pose_display(void);
static void refresh_offset_editors(void);
static int pose_countdown = 0;
static DWORD pose_countdown_next_tick = 0;
static int pose_capture_waiting = 0;
HANDLE HWND_POSE_STATUS;
HANDLE HWND_FRAME_BUTTON;
HANDLE HWND_CAPTURE_MODE_BUTTON;
HANDLE HWND_TRIGGER_THRESHOLD;
HANDLE HWND_TRIGGER_EXPONENT;
HANDLE HWND_TRIGGER_DEADZONE;
HANDLE HWND_TRIGGER_PREVIEW;
HANDLE HWND_CLICK_VALUE;

static void update_trigger_threshold_display(void);

LRESULT CALLBACK WindowProcedure_setup_job(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) /* handle the messages */
  {

  case WM_CREATE:
    editable_control_brush = CreateSolidBrush(editable_control_color);
    break;

  case WM_DESTROY:
    PostQuitMessage(0); /* send a WM_QUIT to the message queue */
    break;
  case WM_COMMAND:

    switch (LOWORD(wParam))
    {
    case offset_dialog_constats + ID_BTN_TOGGLE_FRAME:
      right_hand_frame_local = !right_hand_frame_local;
      write_config(config_file_path);
      SetWindowTextA((HWND)HWND_FRAME_BUTTON,
                     right_hand_frame_local ? "Frame: local" : "Frame: world");
      SetWindowTextA((HWND)HWND_POSE_STATUS,
                     right_hand_frame_local ? "Frame set to local." : "Frame set to world.");
      return 1;

    case offset_dialog_constats + ID_BTN_CAPTURE_POSE:
      pose_countdown = 10;
      pose_countdown_next_tick = GetTickCount() + 1000;
      pose_capture_waiting = 1;
      SetWindowTextA((HWND)HWND_POSE_STATUS, "ADS capture starts in 10 seconds...");
      return 1;

    case offset_dialog_constats + ID_BTN_CAPTURE_MODE:
      capture_ads_apply_position = !capture_ads_apply_position;
      write_config(config_file_path);
      SetWindowTextA((HWND)HWND_CAPTURE_MODE_BUTTON,
                     capture_ads_apply_position ? "Capture: rot+pos" : "Capture: rot-only");
      SetWindowTextA((HWND)HWND_POSE_STATUS,
                     capture_ads_apply_position ? "Capture mode set to rotation+position." : "Capture mode set to rotation-only.");
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_THRESHOLD_DOWN:
      trigger_press_threshold -= 0.01;
      if (trigger_press_threshold < 0.0)
        trigger_press_threshold = 0.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_THRESHOLD_UP:
      trigger_press_threshold += 0.01;
      if (trigger_press_threshold > 1.0)
        trigger_press_threshold = 1.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_EXPONENT_DOWN:
      trigger_curve_exponent -= 0.05;
      if (trigger_curve_exponent < 0.05)
        trigger_curve_exponent = 0.05;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_EXPONENT_UP:
      trigger_curve_exponent += 0.05;
      if (trigger_curve_exponent > 20.0)
        trigger_curve_exponent = 20.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_DEADZONE_DOWN:
      trigger_deadzone -= 0.01;
      if (trigger_deadzone < 0.0)
        trigger_deadzone = 0.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_DEADZONE_UP:
      trigger_deadzone += 0.01;
      if (trigger_deadzone > 0.9)
        trigger_deadzone = 0.9;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_CLICK_DOWN:
      trigger_click_threshold = (trigger_click_threshold < 0.0 ? 0.0 : trigger_click_threshold) - 0.01;
      if (trigger_click_threshold < 0.0)
        trigger_click_threshold = 0.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_CLICK_UP:
      trigger_click_threshold = (trigger_click_threshold < 0.0 ? 0.0 : trigger_click_threshold) + 0.01;
      if (trigger_click_threshold > 1.0)
        trigger_click_threshold = 1.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_CLICK_OFF:
      trigger_click_threshold = -1.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;

    case offset_dialog_constats + ID_BTN_DUMP_ACTIONS:
      InterlockedExchange(&action_dump_requested, 1);
      return 1;

    case offset_dialog_constats + ID_BTN_TRIGGER_RESET:
      trigger_click_threshold = -1.0;
      trigger_force_value = -1.0;
      trigger_range_max = 1.0;
      trigger_press_threshold = 0.5;
      trigger_deadzone = 0.0;
      trigger_curve_exponent = 1.0;
      trigger_gain = 1.0;
      write_config(config_file_path);
      update_trigger_threshold_display();
      return 1;
    }

    if (HIWORD(wParam) == EN_KILLFOCUS)
    {
      char text[64];
      double value;
      HWND control = (HWND)lParam;

      GetWindowTextA(control, text, sizeof(text));
      if (sscanf(text, "%lf", &value) != 1)
        value = 0.0;

      switch (LOWORD(wParam))
      {
      case offset_dialog_constats + ID_LABEL_L_RX: cmd_set_l_rx(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_L_RY: cmd_set_l_ry(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_L_RZ: cmd_set_l_rz(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_L_PX: cmd_set_l_px(value); break;
      case offset_dialog_constats + ID_LABEL_L_PY: cmd_set_l_py(value); break;
      case offset_dialog_constats + ID_LABEL_L_PZ: cmd_set_l_pz(value); break;
      case offset_dialog_constats + ID_LABEL_R_RX: cmd_set_r_rx(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_R_RY: cmd_set_r_ry(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_R_RZ: cmd_set_r_rz(degToRad(value)); break;
      case offset_dialog_constats + ID_LABEL_R_PX: cmd_set_r_px(value); break;
      case offset_dialog_constats + ID_LABEL_R_PY: cmd_set_r_py(value); break;
      case offset_dialog_constats + ID_LABEL_R_PZ: cmd_set_r_pz(value); break;
      }
      return 1;
    }

    if (suppress_button_click && (int)wParam == repeat_button_id)
    {
      suppress_button_click = 0;
      return 1;
    }

    switch (wParam)
    {
    //=========================
    // LEFT
    // rotacao X+
    case offset_dialog_constats + ID_BTN_L_XP_10:
      cmd_set_l_rx(offset_rotation_left_x + degToRad(1.0));
      return 1;
    case offset_dialog_constats + ID_BTN_L_XP_01:
      cmd_set_l_rx(offset_rotation_left_x + degToRad(0.1));
      return 1;

    // rotacao X-
    case offset_dialog_constats + ID_BTN_L_XN_10:
      cmd_set_l_rx(offset_rotation_left_x - degToRad(1.0));
      return 1;

      break;
    case offset_dialog_constats + ID_BTN_L_XN_01:
      cmd_set_l_rx(offset_rotation_left_x - degToRad(0.1));
      return 1;
      break;

    // rotacao Y+
    case offset_dialog_constats + ID_BTN_L_YP_10:
      cmd_set_l_ry(offset_rotation_left_y + degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_YP_01:
      cmd_set_l_ry(offset_rotation_left_y + degToRad(0.1));
      return 1;
      break;

    // rotacao Y-
    case offset_dialog_constats + ID_BTN_L_YN_10:
      cmd_set_l_ry(offset_rotation_left_y - degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_YN_01:
      cmd_set_l_ry(offset_rotation_left_y - degToRad(0.1));
      return 1;
      break;

    // rotacao Z+
    case offset_dialog_constats + ID_BTN_L_ZP_10:
      cmd_set_l_rz(offset_rotation_left_z + degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_ZP_01:
      cmd_set_l_rz(offset_rotation_left_z + degToRad(0.1));
      return 1;
      break;

    // rotacao Z-
    case offset_dialog_constats + ID_BTN_L_ZN_10:
      cmd_set_l_rz(offset_rotation_left_z - degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_ZN_01:
      cmd_set_l_rz(offset_rotation_left_z - degToRad(0.1));
      return 1;
      break;

    // posicao X+
    case offset_dialog_constats + ID_BTN_L_P_XP_10:
      cmd_set_l_px(offset_position_left_x + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_XP_01:
      cmd_set_l_px(offset_position_left_x + 0.01);
      return 1;
      break;

    // posicao X-
    case offset_dialog_constats + ID_BTN_L_P_XN_10:
      cmd_set_l_px(offset_position_left_x - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_XN_01:
      cmd_set_l_px(offset_position_left_x - 0.01);
      return 1;
      break;

    // posicao Y+
    case offset_dialog_constats + ID_BTN_L_P_YP_10:
      cmd_set_l_py(offset_position_left_y + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_YP_01:
      cmd_set_l_py(offset_position_left_y + 0.01);
      return 1;
      break;

    // posicao Y-
    case offset_dialog_constats + ID_BTN_L_P_YN_10:
      cmd_set_l_py(offset_position_left_y - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_YN_01:
      cmd_set_l_py(offset_position_left_y - 0.01);
      return 1;
      break;

    // posicao Z+
    case offset_dialog_constats + ID_BTN_L_P_ZP_10:
      cmd_set_l_pz(offset_position_left_z + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_ZP_01:
      cmd_set_l_pz(offset_position_left_z + 0.01);
      return 1;
      break;

    // posicao Z-
    case offset_dialog_constats + ID_BTN_L_P_ZN_10:
      cmd_set_l_pz(offset_position_left_z - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_L_P_ZN_01:
      cmd_set_l_pz(offset_position_left_z - 0.01);
      return 1;
      break;

    //=========================
    // RIGHT
    // rotacao X+
    case offset_dialog_constats + ID_BTN_R_XP_10:
      cmd_set_r_rx(offset_rotation_right_x + degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_XP_01:
      cmd_set_r_rx(offset_rotation_right_x + degToRad(0.1));
      return 1;
      break;

    // rotacao X-
    case offset_dialog_constats + ID_BTN_R_XN_10:
      cmd_set_r_rx(offset_rotation_right_x - degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_XN_01:
      cmd_set_r_rx(offset_rotation_right_x - degToRad(0.1));
      return 1;
      break;

    // rotacao Y+
    case offset_dialog_constats + ID_BTN_R_YP_10:
      cmd_set_r_ry(offset_rotation_right_y + degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_YP_01:
      cmd_set_r_ry(offset_rotation_right_y + degToRad(0.1));
      return 1;
      break;

    // rotacao Y-
    case offset_dialog_constats + ID_BTN_R_YN_10:
      cmd_set_r_ry(offset_rotation_right_y - degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_YN_01:
      cmd_set_r_ry(offset_rotation_right_y - degToRad(0.1));
      return 1;
      break;

    // rotacao Z+
    case offset_dialog_constats + ID_BTN_R_ZP_10:
      cmd_set_r_rz(offset_rotation_right_z + degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_ZP_01:
      cmd_set_r_rz(offset_rotation_right_z + degToRad(0.1));
      return 1;
      break;

    // rotacao Z-
    case offset_dialog_constats + ID_BTN_R_ZN_10:
      cmd_set_r_rz(offset_rotation_right_z - degToRad(1.0));
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_ZN_01:
      cmd_set_r_rz(offset_rotation_right_z - degToRad(0.1));
      return 1;
      break;

    // posicao X+
    case offset_dialog_constats + ID_BTN_R_P_XP_10:
      cmd_set_r_px(offset_position_right_x + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_XP_01:
      cmd_set_r_px(offset_position_right_x + 0.01);
      return 1;
      break;

    // posicao X-
    case offset_dialog_constats + ID_BTN_R_P_XN_10:
      cmd_set_r_px(offset_position_right_x - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_XN_01:
      cmd_set_r_px(offset_position_right_x - 0.01);
      return 1;
      break;

    // posicao Y+
    case offset_dialog_constats + ID_BTN_R_P_YP_10:
      cmd_set_r_py(offset_position_right_y + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_YP_01:
      cmd_set_r_py(offset_position_right_y + 0.01);
      return 1;
      break;

    // posicao Y-
    case offset_dialog_constats + ID_BTN_R_P_YN_10:
      cmd_set_r_py(offset_position_right_y - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_YN_01:
      cmd_set_r_py(offset_position_right_y - 0.01);
      return 1;
      break;

    // posicao Z+
    case offset_dialog_constats + ID_BTN_R_P_ZP_10:
      cmd_set_r_pz(offset_position_right_z + 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_ZP_01:
      cmd_set_r_pz(offset_position_right_z + 0.01);
      return 1;
      break;

    // posicao Z-
    case offset_dialog_constats + ID_BTN_R_P_ZN_10:
      cmd_set_r_pz(offset_position_right_z - 0.1);
      return 1;
      break;
    case offset_dialog_constats + ID_BTN_R_P_ZN_01:
      cmd_set_r_pz(offset_position_right_z - 0.01);
      return 1;
      break;

    default:;
    }
    break;

  case WM_TIMER:
    if (wParam == GUI_REPEAT_TIMER && repeat_button_id != 0)
      SendMessage(hwnd, WM_COMMAND, repeat_button_id, (LPARAM)repeat_button_hwnd);
    else if (wParam == GUI_POSE_TIMER)
    {
      if (pose_countdown > 0 && (LONG)(GetTickCount() - pose_countdown_next_tick) >= 0)
      {
        pose_countdown--;
        pose_countdown_next_tick += 1000;
        if (pose_countdown > 0)
        {
          char countdown_text[64];
           snprintf(countdown_text, sizeof(countdown_text), "ADS capture starts in %d seconds...", pose_countdown);
          SetWindowTextA((HWND)HWND_POSE_STATUS, countdown_text);
        }
        else
        {
          request_pose_capture();
          SetWindowTextA((HWND)HWND_POSE_STATUS, "ADS capture armed; hold both hands aimed now.");
        }
      }
      update_pose_display();
      if (pose_capture_waiting && pose_countdown == 0)
      {
        PoseDisplaySnapshot right;
        PoseDisplaySnapshot left;
        if (get_pose_display_snapshot(0, &right) && get_pose_display_snapshot(1, &left))
        {
          pose_capture_waiting = 0;
          log_message("ADS pose captured with both hands: right=%f,%f,%f left=%f,%f,%f",
                      right.position_x, right.position_y, right.position_z,
                      left.position_x, left.position_y, left.position_z);
          if (apply_captured_ads_calibration())
          {
            refresh_offset_editors();
            SetWindowTextA((HWND)HWND_POSE_STATUS, "ADS captured and offsets auto-applied.");
          }
          else
          {
            SetWindowTextA((HWND)HWND_POSE_STATUS, "ADS captured, but apply failed.");
          }
        }
      }
    }
    break;
  case WM_KEYDOWN:
    // printf("WM_KEYDOWN: %d\n",wParam,lParam);
    switch (wParam)
    {
    case VK_F1:
      // printf("MODO Manual!\n");
      break;
    case VK_F2:
      // printf("MODO automatico!\n");
      break;
    case VK_F3:
      // send_gcode("!");
      break;
    case VK_F4:
      // send_gcode("@11");
      break;
    case VK_F11:
      /*
      EnableWindow(hwnd_botao_seta_adjm,0);
      EnableWindow(hwnd_botao_seta_adjp,0);
      EnableWindow(hwnd_botao_seta_yp,0);
      EnableWindow(hwnd_botao_seta_ym,0);
      EnableWindow(hwnd_botao_seta_xp,0);
      EnableWindow(hwnd_botao_seta_xm,0);
      EnableWindow(hwnd_botao_seta_xp,0);
      EnableWindow(hwnd_botao_seta_xm,0);
      */
      break;

    case VK_F8:
      // printf("F8 apertado ligar alarme!!\n");
      break;
    }
    break;

  // fazer algo qd o user apertar o bot�o de fechar
  case WM_CLOSE:
    DestroyWindow(hwnd); /* WM_DESTROY posts WM_QUIT, which ends the GUI thread loop */
    return 0;

  default: /* for messages that we don't deal with */
    return DefWindowProc(hwnd, message, wParam, lParam);
  }

  return 0;
}

HANDLE add_label(int x, int y, int tx, int ty, char *text, int id)
{

  int new_id = offset_dialog_constats + id;

  // picking a new id for this control
  HANDLE handle_n = CreateWindow("static", text,        // tipo, rotulo
                                 WS_CHILD | WS_VISIBLE, // flags
                                 x, y,                  // posicao
                                 tx, ty,                // tamanho
                                 hwnd, (HMENU)(INT_PTR)new_id,   // identificacao
                                 hInstance, NULL);

  apply_gui_font(handle_n);
  return handle_n;
}

static HANDLE add_action_button(int x, int y, int tx, int ty, char *text, int id)
{
  int new_id = offset_dialog_constats + id;

  HANDLE handle_n = CreateWindowA("button", text,
                                  WS_CHILD | WS_VISIBLE,
                                  x, y, tx, ty,
                                  hwnd, (HMENU)(INT_PTR)new_id,
                                  hInstance, NULL);

  apply_gui_font(handle_n);
  return handle_n;
}

HANDLE add_edit(int x, int y, int tx, int ty, char *text, int id)
{
  int new_id = offset_dialog_constats + id;
  HANDLE handle_n = CreateWindowA("edit", text,
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                  x, y, tx, ty,
                                  hwnd, (HMENU)(INT_PTR)new_id,
                                  hInstance, NULL);

  apply_gui_font(handle_n);
  return handle_n;
}

HANDLE add_readonly(int x, int y, int tx, int ty, char *text, int id)
{
  int new_id = offset_dialog_constats + id;
  HANDLE handle_n = CreateWindowA("edit", text,
                                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_AUTOHSCROLL,
                                  x, y, tx, ty,
                                  hwnd, (HMENU)(INT_PTR)new_id,
                                  hInstance, NULL);

  apply_gui_font(handle_n);
  return handle_n;
}

static void set_pose_text(HANDLE control, int valid, double value, const char *format)
{
  char text[64];
  if (valid)
    snprintf(text, sizeof(text), format, value);
  else
    snprintf(text, sizeof(text), "--");
  SetWindowTextA((HWND)control, text);
}

static void refresh_offset_editors(void)
{
  char text[64];

  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_left_y));
  SetWindowTextA(HWND_LABEL_L_RY, text);
  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_left_x));
  SetWindowTextA(HWND_LABEL_L_RX, text);
  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_left_z));
  SetWindowTextA(HWND_LABEL_L_RZ, text);

  snprintf(text, sizeof(text), "%1.3f", offset_position_left_x);
  SetWindowTextA(HWND_LABEL_L_PX, text);
  snprintf(text, sizeof(text), "%1.3f", offset_position_left_y);
  SetWindowTextA(HWND_LABEL_L_PY, text);
  snprintf(text, sizeof(text), "%1.3f", offset_position_left_z);
  SetWindowTextA(HWND_LABEL_L_PZ, text);

  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_right_y));
  SetWindowTextA(HWND_LABEL_R_RY, text);
  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_right_x));
  SetWindowTextA(HWND_LABEL_R_RX, text);
  snprintf(text, sizeof(text), "%1.2f", radToDeg(offset_rotation_right_z));
  SetWindowTextA(HWND_LABEL_R_RZ, text);

  snprintf(text, sizeof(text), "%1.3f", offset_position_right_x);
  SetWindowTextA(HWND_LABEL_R_PX, text);
  snprintf(text, sizeof(text), "%1.3f", offset_position_right_y);
  SetWindowTextA(HWND_LABEL_R_PY, text);
  snprintf(text, sizeof(text), "%1.3f", offset_position_right_z);
  SetWindowTextA(HWND_LABEL_R_PZ, text);
}

static void update_pose_display(void)
{
  PoseDisplaySnapshot left;
  PoseDisplaySnapshot right;
  int left_valid = get_pose_display_snapshot(1, &left);
  int right_valid = get_pose_display_snapshot(0, &right);

  set_pose_text(HWND_POSE_L_RY, left_valid, left.rotation_y, "%.2f");
  set_pose_text(HWND_POSE_L_RX, left_valid, left.rotation_x, "%.2f");
  set_pose_text(HWND_POSE_L_RZ, left_valid, left.rotation_z, "%.2f");
  set_pose_text(HWND_POSE_L_PX, left_valid, left.position_x, "%.3f");
  set_pose_text(HWND_POSE_L_PY, left_valid, left.position_y, "%.3f");
  set_pose_text(HWND_POSE_L_PZ, left_valid, left.position_z, "%.3f");
  set_pose_text(HWND_POSE_R_RY, right_valid, right.rotation_y, "%.2f");
  set_pose_text(HWND_POSE_R_RX, right_valid, right.rotation_x, "%.2f");
  set_pose_text(HWND_POSE_R_RZ, right_valid, right.rotation_z, "%.2f");
  set_pose_text(HWND_POSE_R_PX, right_valid, right.position_x, "%.3f");
  set_pose_text(HWND_POSE_R_PY, right_valid, right.position_y, "%.3f");
  set_pose_text(HWND_POSE_R_PZ, right_valid, right.position_z, "%.3f");
}

static void update_trigger_threshold_display(void)
{
  char text[160];
  CoreTriggerCfg cfg;
  snprintf(text, sizeof(text), "%1.1f", trigger_press_threshold * 100.0);
  SetWindowTextA((HWND)HWND_TRIGGER_THRESHOLD, text);
  snprintf(text, sizeof(text), "%1.2f", trigger_curve_exponent);
  SetWindowTextA((HWND)HWND_TRIGGER_EXPONENT, text);
  snprintf(text, sizeof(text), "%1.1f", trigger_deadzone * 100.0);
  SetWindowTextA((HWND)HWND_TRIGGER_DEADZONE, text);
  if (trigger_click_threshold < 0.0)
    snprintf(text, sizeof(text), "-1 (hardware)");
  else
    snprintf(text, sizeof(text), "%1.2f", trigger_click_threshold);
  SetWindowTextA((HWND)HWND_CLICK_VALUE, text);

  core_trigger_cfg_defaults(&cfg);
  cfg.deadzone = (float)trigger_deadzone;
  cfg.exponent = (float)trigger_curve_exponent;
  cfg.press_threshold = (float)trigger_press_threshold;
  cfg.gain = (float)trigger_gain;
  core_trigger_cfg_sanitize(&cfg);
  snprintf(text, sizeof(text), "Preview raw->out: 25%%=%.0f%%  50%%=%.0f%%  75%%=%.0f%%  (press at curved >= %.0f%%)",
           core_trigger_output(0.25f, &cfg) * 100.0, core_trigger_output(0.5f, &cfg) * 100.0,
           core_trigger_output(0.75f, &cfg) * 100.0, trigger_press_threshold * 100.0);
  SetWindowTextA((HWND)HWND_TRIGGER_PREVIEW, text);
}

LRESULT CALLBACK repeat_button_proc(HWND button, UINT message, WPARAM wParam, LPARAM lParam)
{
  WNDPROC original = (WNDPROC)GetPropA(button, "OpenXROriginalButtonProc");

  if (message == WM_LBUTTONDOWN)
  {
    repeat_button_id = GetDlgCtrlID(button);
    repeat_button_hwnd = button;
    suppress_button_click = 0;
    SendMessage(hwnd, WM_COMMAND, repeat_button_id, (LPARAM)button);
    SetCapture(button);
    SetTimer(hwnd, GUI_REPEAT_TIMER, 75, NULL);
  }
  else if (message == WM_LBUTTONUP)
  {
    if (repeat_button_hwnd == button)
    {
      KillTimer(hwnd, GUI_REPEAT_TIMER);
      repeat_button_id = 0;
      repeat_button_hwnd = NULL;
      suppress_button_click = 1;
      ReleaseCapture();
    }
  }

  return CallWindowProcA(original, button, message, wParam, lParam);
}

HANDLE add_button(int x, int y, int tx, int ty, char *text, int id)
{
  int new_id = offset_dialog_constats + id;

  // picking a new id for this control
  HANDLE handle_n = CreateWindow("button", text,        // tipo, rotulo
                                 WS_CHILD | WS_VISIBLE, // flags
                                 x, y,                  // posicao
                                 tx, ty,                // tamanho
                                 hwnd, (HMENU)(INT_PTR)new_id,   // identificacao
                                 hInstance, NULL);

  apply_gui_font(handle_n);
  if (handle_n)
  {
    WNDPROC original = (WNDPROC)SetWindowLongPtrA((HWND)handle_n, GWLP_WNDPROC,
                                                  (LONG_PTR)repeat_button_proc);
    SetPropA((HWND)handle_n, "OpenXROriginalButtonProc", (HANDLE)original);
  }

  return handle_n;
}

// fues q comunicam a gui e a thread principal de mudan�as
void cmd_set_l_rx(double val)
{
  // potencial treta de threads...
  offset_rotation_left_x = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_L_RX, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_L_RX, st_btn);

  write_config(config_file_path);
}

void cmd_set_l_ry(double val)
{
  // potencial treta de threads...
  offset_rotation_left_y = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_L_RY, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_L_RY, st_btn);

  write_config(config_file_path);
}

void cmd_set_l_rz(double val)
{
  // potencial treta de threads...
  offset_rotation_left_z = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_L_RZ, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_L_RZ, st_btn);

  write_config(config_file_path);
}

void cmd_set_l_px(double val)
{

  offset_position_left_x = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_L_PX, st_btn);

  write_config(config_file_path);
}

void cmd_set_l_py(double val)
{
  offset_position_left_y = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_L_PY, st_btn);

  write_config(config_file_path);
}

void cmd_set_l_pz(double val)
{
  offset_position_left_z = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_L_PZ, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_rx(double val)
{
  // potencial treta de threads...
  offset_rotation_right_x = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_R_RX, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_R_RX, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_ry(double val)
{
  // potencial treta de threads...
  offset_rotation_right_y = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_R_RY, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_R_RY, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_rz(double val)
{
  // potencial treta de threads...
  offset_rotation_right_z = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", radToDeg(val));
  // SendMessage(HWND_LABEL_R_RZ, WM_SETTEXT, 0, st_btn);
  SetWindowTextA(HWND_LABEL_R_RZ, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_px(double val)
{
  offset_position_right_x = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_R_PX, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_py(double val)
{
  offset_position_right_y = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_R_PY, st_btn);

  write_config(config_file_path);
}

void cmd_set_r_pz(double val)
{
  offset_position_right_z = val;

  char st_btn[16];
  sprintf(st_btn, "%1.2f", val);
  SetWindowTextA(HWND_LABEL_R_PZ, st_btn);

  write_config(config_file_path);
}

char szClassName[] = "openxr offsets thingy halfassed";

void create_gui()
{

  // printf("Pasta: %s\n",ret_browsefolder());

  hInstance = GetModuleHandle(0);

  /* The Window structure */
  wincl.hInstance = hInstance;
  wincl.lpszClassName = szClassName;
  wincl.lpfnWndProc = WindowProcedure_setup_job; /* This function is called by windows */
  wincl.style = CS_DBLCLKS;                      /* Catch double-clicks */
  wincl.cbSize = sizeof(WNDCLASSEX);

  /* Use default icon and mouse-pointer */
  wincl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wincl.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
  wincl.hCursor = LoadCursor(NULL, IDC_ARROW);
  wincl.lpszMenuName = NULL; /* No menu */
  wincl.cbClsExtra = 0;      /* No extra bytes after the window class */
  wincl.cbWndExtra = 0;      /* structure or the window instance */
  /* Use Windows's default color as the background of the window */
  wincl.hbrBackground = (HBRUSH)COLOR_BACKGROUND;

  /* Register the window class, and if it fails quit the program */
  if (!RegisterClassEx(&wincl))
    return;

  /* The class is registered, let's create the program*/
  hwnd = CreateWindowEx(
      0,                                        /* Extended possibilites for variation */
      szClassName,                              /* Classname */
      "OpenXR controller offsets",              /* Title Text */
      WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, /* default window */
      CW_USEDEFAULT,                            /* Windows decides the position */
      CW_USEDEFAULT,                            /* where the window ends up on the screen */
      820,                                      /* The programs width */
       760,                                      /* and height in pixels */
      HWND_DESKTOP,                             /* The window is a child-window to desktop */
      NULL,                                     /* No menu */
      hInstance,                                /* Program Instance handler */
      NULL                                      /* No Window Creation data */
  );

  //
  // int x_setas = 50;
  // int y_setas = 100;
  // int setas_tam_btn = 40;

  //
  int yo;
  int xo;
  char stlabel[32];

  //================================================
  // menu superior
  add_label(270 + 52, 10, 100, 20, "left controller", 0);

  yo = 70;
  xo = 0;

  add_label(xo + 5, yo - 55, 110, 20, "Yaw (_y) [deg]:", 0);

  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_left_y));
  HWND_LABEL_L_RY = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_L_RY);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_L_YP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_L_YN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_L_YP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_L_YN_01);

  yo = 70;
  xo = 120;

  add_label(xo + 5, yo - 55, 110, 20, "Pitch (_x) [deg]:", 0);
  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_left_x));
  HWND_LABEL_L_RX = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_L_RX);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_L_XP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_L_XN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_L_XP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_L_XN_01);

  yo = 70;
  xo = 240;

  add_label(xo + 5, yo - 55, 110, 20, "Roll (_z) [deg]:", 0);
  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_left_z));
  HWND_LABEL_L_RZ = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_L_RZ);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_L_ZP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_L_ZN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_L_ZP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_L_ZN_01);

  // #####################

  yo = 70;
  xo = 360;

  add_label(xo + 25, yo - 30, 70, 20, "X [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_left_x);
  HWND_LABEL_L_PX = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_L_PX);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_L_P_XP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_L_P_XN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_L_P_XP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_L_P_XN_01);

  yo = 70;
  xo = 480;

  add_label(xo + 25, yo - 30, 70, 20, "Y [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_left_y);
  HWND_LABEL_L_PY = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_L_PY);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_L_P_YP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_L_P_YN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_L_P_YP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_L_P_YN_01);

  yo = 70;
  xo = 600;

  add_label(xo + 25, yo - 30, 70, 20, "Z [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_left_z);
  HWND_LABEL_L_PZ = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_L_PZ);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_L_P_ZP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_L_P_ZN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_L_P_ZP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_L_P_ZN_01);

  //==========================================
  add_label(270 + 52, 210, 100, 20, "right controller", 0);

  yo = 270;
  xo = 0;

  add_label(xo + 5, yo - 55, 110, 20, "Yaw (_y) [deg]:", 0);
  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_right_y));
  HWND_LABEL_R_RY = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_R_RY);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_R_YP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_R_YN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_R_YP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_R_YN_01);

  yo = 270;
  xo = 120;

  add_label(xo + 5, yo - 55, 110, 20, "Pitch (_x) [deg]:", 0);
  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_right_x));
  HWND_LABEL_R_RX = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_R_RX);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_R_XP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_R_XN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_R_XP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_R_XN_01);

  yo = 270;
  xo = 240;

  add_label(xo + 5, yo - 55, 110, 20, "Roll (_z) [deg]:", 0);
  sprintf(stlabel, "%1.2f", radToDeg(offset_rotation_right_z));
  HWND_LABEL_R_RZ = add_edit(xo + 65, yo - 30, 50, 20, stlabel, ID_LABEL_R_RZ);

  add_button(xo + 10, yo, 50, 50, "+1.0", ID_BTN_R_ZP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-1.0", ID_BTN_R_ZN_10);

  add_button(xo + 65, yo, 50, 50, "+0.1", ID_BTN_R_ZP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.1", ID_BTN_R_ZN_01);

  // #################

  yo = 270;
  xo = 360;

  add_label(xo + 25, yo - 30, 70, 20, "X [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_right_x);
  HWND_LABEL_R_PX = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_R_PX);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_R_P_XP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_R_P_XN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_R_P_XP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_R_P_XN_01);

  yo = 270;
  xo = 480;

  add_label(xo + 25, yo - 30, 70, 20, "Y [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_right_y);
  HWND_LABEL_R_PY = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_R_PY);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_R_P_YP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_R_P_YN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_R_P_YP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_R_P_YN_01);

  yo = 270;
  xo = 600;

  add_label(xo + 25, yo - 30, 70, 20, "Z [m]:", 0);
  sprintf(stlabel, "%1.3f", offset_position_right_z);
  HWND_LABEL_R_PZ = add_edit(xo + 70, yo - 30, 50, 20, stlabel, ID_LABEL_R_PZ);

  add_button(xo + 10, yo, 50, 50, "+0.10", ID_BTN_R_P_ZP_10);
  add_button(xo + 10, yo + 50, 50, 50, "-0.10", ID_BTN_R_P_ZN_10);

  add_button(xo + 65, yo, 50, 50, "+0.01", ID_BTN_R_P_ZP_01);
  add_button(xo + 65, yo + 50, 50, 50, "-0.01", ID_BTN_R_P_ZN_01);

#ifdef BASE_SPACE_DIAGNOSTIC
  add_label(10, 365, 500, 20, "ADS reference: hold both hands aimed (degrees / meters):", 0);
#else
  add_label(10, 365, 500, 20, "ADS reference, both hands relative to headset (degrees / meters):", 0);
#endif

  add_label(10, 390, 90, 20, "L Yaw (_y) [deg]", 0);
  add_label(115, 390, 90, 20, "L Pitch (_x) [deg]", 0);
  add_label(220, 390, 90, 20, "L Roll (_z) [deg]", 0);
  add_label(325, 390, 70, 20, "L X [m]", 0);
  add_label(430, 390, 70, 20, "L Y [m]", 0);
  add_label(535, 390, 70, 20, "L Z [m]", 0);
  HWND_POSE_L_RX = add_readonly(10, 410, 80, 20, "--", ID_POSE_L_RX);
  HWND_POSE_L_RY = add_readonly(115, 410, 80, 20, "--", ID_POSE_L_RY);
  HWND_POSE_L_RZ = add_readonly(220, 410, 80, 20, "--", ID_POSE_L_RZ);
  HWND_POSE_L_PX = add_readonly(325, 410, 80, 20, "--", ID_POSE_L_PX);
  HWND_POSE_L_PY = add_readonly(430, 410, 80, 20, "--", ID_POSE_L_PY);
  HWND_POSE_L_PZ = add_readonly(535, 410, 80, 20, "--", ID_POSE_L_PZ);

  add_label(10, 450, 90, 20, "R Yaw (_y) [deg]", 0);
  add_label(115, 450, 90, 20, "R Pitch (_x) [deg]", 0);
  add_label(220, 450, 90, 20, "R Roll (_z) [deg]", 0);
  add_label(325, 450, 70, 20, "R X [m]", 0);
  add_label(430, 450, 70, 20, "R Y [m]", 0);
  add_label(535, 450, 70, 20, "R Z [m]", 0);
  HWND_POSE_R_RX = add_readonly(10, 470, 80, 20, "--", ID_POSE_R_RX);
  HWND_POSE_R_RY = add_readonly(115, 470, 80, 20, "--", ID_POSE_R_RY);
  HWND_POSE_R_RZ = add_readonly(220, 470, 80, 20, "--", ID_POSE_R_RZ);
  HWND_POSE_R_PX = add_readonly(325, 470, 80, 20, "--", ID_POSE_R_PX);
  HWND_POSE_R_PY = add_readonly(430, 470, 80, 20, "--", ID_POSE_R_PY);
  HWND_POSE_R_PZ = add_readonly(535, 470, 80, 20, "--", ID_POSE_R_PZ);

  add_action_button(10, 515, 150, 30, "Capture ADS (10s)", ID_BTN_CAPTURE_POSE);
  HWND_FRAME_BUTTON = add_action_button(170, 515, 120, 30,
                                        right_hand_frame_local ? "Frame: local" : "Frame: world",
                                        ID_BTN_TOGGLE_FRAME);
  HWND_CAPTURE_MODE_BUTTON = add_action_button(300, 515, 140, 30,
                                               capture_ads_apply_position ? "Capture: rot+pos" : "Capture: rot-only",
                                               ID_BTN_CAPTURE_MODE);
  HWND_POSE_STATUS = add_label(450, 515, 240, 30, "Ready.", ID_LABEL_POSE_STATUS);
  add_label(10, 550, 185, 20, "Trigger threshold [%]:", 0);
  add_label(520, 530, 290, 40, "Boolean press threshold (0-1). Some games fire from the analog axis instead; for those, use trigger_deadzone and trigger_curve_exponent.", 0);
  snprintf(stlabel, sizeof(stlabel), "%1.1f", trigger_press_threshold * 100.0);
  HWND_TRIGGER_THRESHOLD = add_readonly(200, 550, 60, 20, stlabel, ID_LABEL_TRIGGER_THRESHOLD);
  add_button(265, 550, 55, 24, "-1%", ID_BTN_TRIGGER_THRESHOLD_DOWN);
  add_button(325, 550, 55, 24, "+1%", ID_BTN_TRIGGER_THRESHOLD_UP);
  add_button(390, 550, 120, 24, "Reset trigger", ID_BTN_TRIGGER_RESET);

  add_label(10, 580, 330, 20, "Trigger curve (exponent): 1.0 = linear, >1 = easier pull, <1 = deeper pull", 0);
  snprintf(stlabel, sizeof(stlabel), "%1.2f", trigger_curve_exponent);
  HWND_TRIGGER_EXPONENT = add_readonly(345, 580, 60, 20, stlabel, ID_LABEL_TRIGGER_EXPONENT);
  add_button(410, 580, 55, 24, "-0.05", ID_BTN_TRIGGER_EXPONENT_DOWN);
  add_button(470, 580, 55, 24, "+0.05", ID_BTN_TRIGGER_EXPONENT_UP);

  add_label(10, 610, 185, 20, "Trigger deadzone [%]:", 0);
  snprintf(stlabel, sizeof(stlabel), "%1.1f", trigger_deadzone * 100.0);
  HWND_TRIGGER_DEADZONE = add_readonly(200, 610, 60, 20, stlabel, ID_LABEL_TRIGGER_DEADZONE);
  add_button(265, 610, 55, 24, "-1%", ID_BTN_TRIGGER_DEADZONE_DOWN);
  add_button(325, 610, 55, 24, "+1%", ID_BTN_TRIGGER_DEADZONE_UP);
  HWND_TRIGGER_PREVIEW = add_label(390, 612, 420, 20, "", ID_LABEL_TRIGGER_PREVIEW);
  add_label(10, 640, 400, 20, "Trigger click threshold: -1 = use hardware click, 0-1 = synthesize", 0);
  HWND_CLICK_VALUE = add_readonly(415, 640, 90, 20, "-1 (hardware)", ID_LABEL_CLICK);
  add_button(510, 640, 55, 24, "-0.01", ID_BTN_CLICK_DOWN);
  add_button(570, 640, 55, 24, "+0.01", ID_BTN_CLICK_UP);
  add_button(630, 640, 60, 24, "-1 (off)", ID_BTN_CLICK_OFF);
  add_button(10, 668, 180, 24, "Dump action table", ID_BTN_DUMP_ACTIONS);
  update_trigger_threshold_display();

  // tamanho ajuste

  if (hwnd == NULL)
  {
    // printf("Falha ao criar a janela!\n");
    return;
  }
  else
  {
    ShowWindow(hwnd, SW_SHOW);
    update_pose_display();
    SetTimer(hwnd, GUI_POSE_TIMER, 100, NULL);
    // printf("janela criada com sucesso!\n");
  }
}

int kill_gui()
{
  flag_quit = 1;
  return 1;
}

int gui_doevents()
{
  MSG msg;

  // enquanto n�s temos novas mensagens
  // while (GetMessage(&msg, NULL, 0, 0))
  while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
  {
    // Manuzear as mensagns
    switch (msg.message)
    {
    // casos de fechar a engine
    case WM_QUIT:
    case WM_CLOSE:
      flag_quit = 1; // pediram para eu fechar
      break;

    default:;
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  return flag_quit;
}

DWORD WINAPI thread_gui(void *ptr)
{

// wait a bit before actually launching the gui
// unity seems to do some funky stuff with windows that
// its process owns at startup
#ifndef DEBUG
  Sleep(8000);
#endif // DEBUG

  // creates the form
  create_gui();

  // do the message pump stuff
  while (!gui_doevents())
  {
    Sleep(5);
  }

  return 0;
}

int launch_gui_thread(void *data)
{
  char mutex_name[64];
  snprintf(mutex_name, sizeof(mutex_name), "Local\\OpenXRControllerOffsetsGui-%lu",
           (unsigned long)GetCurrentProcessId());
  gui_instance_mutex = CreateMutexA(NULL, FALSE, mutex_name);
  if (gui_instance_mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
  {
    if (gui_instance_mutex != NULL)
    {
      CloseHandle(gui_instance_mutex);
      gui_instance_mutex = NULL;
    }
    return 0;
  }

  HANDLE hthread;
  DWORD dwThreadId;

  hthread = CreateThread(
      NULL,         // default security attributes
      0,            // use default stack size
      thread_gui,   // thread function name
      data,         // argument to thread function
      0,            // use default creation flags
      &dwThreadId); // returns the thread identifier

  return hthread != NULL;
}
