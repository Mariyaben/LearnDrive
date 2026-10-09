#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ld_engine ld_engine;
typedef struct { double time,speed,pitch,acceleration,progress; int calibrated,speed_valid,pitch_valid,braking,hard_braking,shock; } ld_telemetry;
ld_engine* ld_create(void);
void ld_destroy(ld_engine*);
void ld_reset(ld_engine*);
void ld_imu(ld_engine*,double time,const double acceleration[3],const double gyro[3],const double linear[3],int has_linear);
int ld_gps(ld_engine*,double time,double speed_mps,double accuracy_mps);
ld_telemetry ld_snapshot(ld_engine*,double time);
// valid, pitch degrees, roll degrees, grade percent, normalization progress.
int ld_inclination(ld_engine*,double time,double* out,size_t capacity);
const char* ld_calibration_reason(ld_engine*);
ld_telemetry ld_advice_telemetry(ld_telemetry measured,int require_gps,double manual_speed_kmh);
ld_telemetry ld_video_telemetry(ld_telemetry measured,double video_speed_kmh,double test_pitch_degrees);
ld_telemetry ld_video_speed_telemetry(ld_telemetry measured,double video_speed_kmh);
int ld_decide(ld_engine*,ld_telemetry,double hazard_time,double tti,double confidence,int in_path,int audio_ready,int allow_gear);
void ld_accepted(ld_engine*,double time);
void ld_camera_started(ld_engine*,double time);
int ld_camera_decide(ld_engine*,double time,int audio_ready,int braking,int shock);
int ld_camera_decide_telemetry(ld_engine*,ld_telemetry,int audio_ready,int manual);
void ld_camera_accepted(ld_engine*,int prompt,double time);
const char* ld_camera_reason(ld_engine*);
void ld_driving_reset(ld_engine*);
// Separate APIs preserve the existing telemetry ABI. RPM bands are vehicle
// specific and must be supplied by a live adapter (or the explicit Desk demo).
void ld_engine_sample(ld_engine*,double time,int gear,int max_gear,double rpm,double lower_rpm,double upper_rpm,int under_load);
int ld_driving_decide(ld_engine*,ld_telemetry,int audio_ready,int manual,int slope_enabled);
void ld_driving_accepted(ld_engine*,int prompt,double time);
const char* ld_driving_reason(ld_engine*);
int ld_prompt_priority(int prompt);
int ld_prompt_count(void);
int ld_slope_band(double normalized_pitch_degrees);
int ld_prompt_may_interrupt(int next,int current);
const char* ld_prompt_text(int prompt);
// time, valid, bend (-1/0/+1), then five normalized x,y point pairs.
int ld_road_shape(ld_engine*,double* out,size_t capacity);
// All C ABI functions catch exceptions. Check error text after a failed operation.
const char* ld_error(ld_engine*);
const char* ld_decision_reason(ld_engine*);
// Packs x,y,width,height,score,COCO category per box. Returns boxes written.
size_t ld_boxes(ld_engine*,float* out,size_t capacity_floats);
int ld_preprocess(ld_engine*,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int width,int height,int rotation,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zero_point,int full_range,int save_gray);
int ld_vision(ld_engine*,double time,const float* detections,size_t count,int channels,int candidates,int channel_first,int normalized,const float* road,const float* lane,int mask_width,int mask_height,double hazard_out[3]);
#ifdef __cplusplus
}
#endif
