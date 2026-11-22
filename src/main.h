#ifndef __MAIN_H__
#define __MAIN_H__

#include <windows.h>

/*  To use this exported function of dll, include this header
 *  in your project.
 */

#ifdef BUILD_DLL
    #define DLL_EXPORT __declspec(dllexport)
#else
    #define DLL_EXPORT __declspec(dllimport)
#endif


#ifdef __cplusplus
extern "C"
{
#endif

//void DLL_EXPORT SomeFunction(const LPCSTR sometext);


typedef struct {
    char config_fp[256];
} thread_gui_data;

typedef struct {
    int valid;
    double rotation_x;
    double rotation_y;
    double rotation_z;
    double position_x;
    double position_y;
    double position_z;
} PoseDisplaySnapshot;


extern double offset_rotation_left_x;
extern double offset_rotation_left_y;
extern double offset_rotation_left_z;

extern double offset_rotation_right_x;
extern double offset_rotation_right_y;
extern double offset_rotation_right_z;

extern double offset_position_left_x;
extern double offset_position_left_y;
extern double offset_position_left_z;

extern double offset_position_right_x;
extern double offset_position_right_y;
extern double offset_position_right_z;
extern int right_hand_frame_local;
extern int capture_ads_apply_position;
extern double trigger_press_threshold;
extern double trigger_deadzone;
extern double trigger_curve_exponent;
extern double trigger_gain;

extern char config_file_path[256];

int write_config(const char *config_file);
int get_pose_display_snapshot(int left, PoseDisplaySnapshot *snapshot);
void request_pose_capture(void);
int apply_captured_ads_calibration(void);
int log_message(const char *fmt, ...);

//int log_message(const char *fmt, ...);


#ifdef __cplusplus
}
#endif

#endif // __MAIN_H__
