#ifndef PSP_TITLE_TRACE_H
#define PSP_TITLE_TRACE_H

#include "PR/ultratypes.h"

#ifndef SF64_PSP_PROFILE_FRAME_TRACE
#define SF64_PSP_PROFILE_FRAME_TRACE 0
#endif

#if SF64_PSP_PROFILE_FRAME_TRACE
typedef struct {
    s32 valid;
    s32 title_state;
    s32 cutscene_state;
    s32 scene_state;
    s32 timer1;
    s32 timer2;
    s32 timer3;
    s32 title_msg_frame_count;
    s32 title_hold_timer;
    s32 selected_team;
    s32 team_frame_count[4];
    s32 team_frame_step[4];
    s32 team_motion_enabled[4];
    f32 light_pitch;
    f32 light_yaw;
    f32 team_light_dir_x;
    f32 team_light_dir_y;
    f32 team_light_dir_z;
    f32 light_target_x;
    f32 light_target_y;
    f32 light_target_z;
    f32 team_ambient_r;
    f32 team_ambient_g;
    f32 team_ambient_b;
    f32 camera_eye_x;
    f32 camera_eye_y;
    f32 camera_eye_z;
    f32 camera_at_x;
    f32 camera_at_y;
    f32 camera_at_z;
    u32 flags;
} PspTitleTraceMarkers;

void Title_PspGetTraceMarkers(PspTitleTraceMarkers* markers);
#endif

#endif
