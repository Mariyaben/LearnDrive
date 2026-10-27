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
// Successful normalization generation, monotonic for this engine's lifetime.
// An ID alone does not confirm level ground or current sensor freshness.
uint64_t ld_calibration_id(ld_engine*);
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
// Deterministic dual-brain policy over the most recent native vision scene.
// Returns a Prompt ID and writes whether lower-priority alerts should be suppressed.
int ld_dual_policy_decide(ld_engine*,ld_telemetry,double now,int* suppress_other_alerts);
void ld_dual_policy_accepted(ld_engine*,int prompt,double time);
void ld_dual_policy_reset(ld_engine*);
const char* ld_dual_policy_reason(ld_engine*);
enum { LD_SOURCE_CAMERA=0, LD_SOURCE_RECORDING=1, LD_SOURCE_SIMULATION=2, LD_SOURCE_SCREEN=3 };
enum { LD_GUIDANCE_NONE=0, LD_GUIDANCE_REFLEX=1, LD_GUIDANCE_TACTICAL=2, LD_GUIDANCE_TERRAIN=3, LD_GUIDANCE_CAMERA=4, LD_GUIDANCE_NAVIGATION=5 };

// Cold route load; lat/lon pairs and (shape index, Valhalla maneuver type) pairs.
// Coordinates stay local and are not added to ordinary diagnostic exports.
int ld_navigation_route(ld_engine*,uint64_t route_id,const double* coordinates,size_t points,const int* maneuvers,size_t count);
// Provider-neutral structured feed. These are LearnDrive-owned constants, not
// raw SDK enum values. A session token is strictly increasing across journeys;
// route_epoch increases when route/step numbering changes; sequence increases
// for every callback. received_at uses the engine's monotonic seconds clock.
enum { LD_TURN_UNKNOWN=0,LD_TURN_NAVIGATING=1,LD_TURN_REROUTING=2,LD_TURN_STOPPED=3,LD_TURN_ARRIVED=4 };
enum { LD_MANEUVER_UNKNOWN=0,LD_MANEUVER_LEFT=1,LD_MANEUVER_RIGHT=2,LD_MANEUVER_FORK=3,
    LD_MANEUVER_RAMP=4,LD_MANEUVER_MERGE=5,LD_MANEUVER_UTURN=6,LD_MANEUVER_ROUNDABOUT=7,
    LD_MANEUVER_STRAIGHT=8,LD_MANEUVER_DESTINATION=9 };
typedef struct {
    int provider;
    uint64_t session,route_epoch,sequence;
    double received_at;
    int state,step_number,maneuver;
    double estimated_remaining_m,estimated_remaining_seconds;
} ld_navigation_turn;
// step_number=-1 and estimates=+infinity represent unavailable values. Only
// ordinary left/right preparation is eligible for speech; no polyline anchors
// or exact-distance assertions are invented from this feed. GPS is independent.
int ld_navigation_turn_update(ld_engine*,const ld_navigation_turn*);
// Temporary feed loss/pause, unlike explicit clear: preserves session
// tombstones and measured GPS but requires a newer accepted feed to recover.
void ld_navigation_turn_unavailable(ld_engine*);
void ld_navigation_clear(ld_engine*);
int ld_navigation_fix(ld_engine*,uint64_t source_id,double time,double latitude,double longitude,double accuracy_m,
    double speed_kmh,double speed_accuracy_mps,int speed_valid,double bearing_degrees,int bearing_valid);
// route id, feature id, feature, match, fix time, progress, distance, heading
// change, estimated radius, speed limit (unknown=inf), context valid,
// next visual maneuver type, shape index, distance, raw GPS speed, speed accuracy,
// horizontal accuracy. Unknown values are infinite; capacity must be >=17.
int ld_navigation_context(ld_engine*,double now,double* out,size_t capacity);
const char* ld_navigation_status(ld_engine*,double now);
enum { LD_GUIDANCE_OK=0, LD_GUIDANCE_INACTIVE=1, LD_GUIDANCE_AUDIO=2, LD_GUIDANCE_COOLDOWN=3,
       LD_GUIDANCE_RESEARCH_ONLY=4, LD_GUIDANCE_INVALID=5 };
typedef struct {
    uint64_t source_id;
    ld_telemetry telemetry;
    double now,hazard_time,tti,confidence; // Hazard fields are used only by explicit Desk simulation.
    int in_path,active,audio_ready,manual,slope_enabled,allow_emergency_commands;
} ld_guidance_request;
typedef struct {
    uint64_t sequence,source_id;
    int prompt,origin,tactical,reflex,terrain,camera,suppress,eligible,blocked;
    double capture_time,scene_age;
    int semantic_available,spatial_available;
    uint64_t actor_id; // Source-local camera attention track; zero means unattributed.
    int actor_category,evidence_kind; // COCO class; 1 sustained attention, 2 optical approach, 3 critical.
    double actor_x,actor_y,actor_w,actor_h; // Capture-frame normalized box, not metric position.
} ld_guidance_result;
// Monotonic source IDs invalidate stale inference; global audio cooldown survives.
int ld_guidance_begin(ld_engine*,uint64_t source_id,int source_kind,double now);
int ld_guidance_decide(ld_engine*,const ld_guidance_request*,ld_guidance_result*);
int ld_guidance_accepted(ld_engine*,uint64_t sequence,int prompt,double now);
int ld_guidance_pending(ld_engine*,uint64_t sequence,int prompt,double now);
const char* ld_guidance_reason(ld_engine*);
// Opt-in coaching coordinator; old callers remain on the frozen legacy path.
// Profiles: 0 Minimal (20s), 1 Balanced (12s), 2 Guided (8s). Timings experimental.
// revision>=1; identical same-revision configuration is idempotent. A newer
// revision invalidates pending/accepted tickets while preserving pacing.
int ld_coaching_configure(ld_engine*,int profile,int voice,uint64_t revision);
// Logical/visual presentation is distinct from PCM queue submission. Neither
// submission nor onset means the driver heard or followed the cue.
int ld_coaching_accept(ld_engine*,uint64_t sequence,int prompt,double now,int voice_submitted);
int ld_coaching_playback_valid(ld_engine*,uint64_t sequence,int prompt,double now,uint64_t revision);
void ld_coaching_playback_started(ld_engine*,uint64_t sequence,int prompt,double now,uint64_t revision);
// 20 values: enabled,profile,voice,revision,mode,block,event,capture,age,
// historyCount,activeEvents,resolvedEvents,logicalAccepted,voiceSubmitted,
// playbackOnsets,onsetRejected,duplicates,deferred,speedTrend,ordinaryInterval.
// mode 0 SILENCE/1 COACH/2 WARN/3 DEFER; trend 0 unknown/1 steady/2 up/3 down.
// No images, coordinates, road names, routes or free-form speech are exported.
int ld_coaching_diagnostics(ld_engine*,double now,double* out,size_t capacity);
const char* ld_coaching_diagnostics_json(ld_engine*,double now);
// Version2 scalar interpreted context; Google-derived provider context is excluded.
const char* ld_coaching_context_json(ld_engine*);
int ld_coaching_delivery_event(ld_engine*,uint64_t sequence,int prompt,double now,int stage);
int ld_spatial_publish_source(ld_engine*,uint64_t source_id,double time,const float*,size_t);
// Lost pose/optics invalidates spatial and derived metric evidence immediately.
// Preserves semantic boxes and speech cooldowns; rejects old-source/older work.
int ld_spatial_unavailable_source(ld_engine*,uint64_t source_id,double capture_time);
int ld_spatial_semantics_source(ld_engine*,uint64_t source_id,double time,const float*,size_t,int,int,double out[3]);
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
void ld_camera_intrinsics(ld_engine*,double focal_length_y_pixels);
int ld_camera_up(ld_engine*,double time,double out[3]);
int ld_spatial_publish(ld_engine*,double capture_time,const float* output,size_t count);
int ld_spatial_geometry(ld_engine*,const double geometry[9]);
int ld_spatial_semantics(ld_engine*,double time,const float* detections,size_t count,int candidates,int channel_first,double hazard[3]);
int ld_audio_allowed(ld_engine*,int prompt,double now);
void ld_audio_accepted(ld_engine*,int prompt,double now);
typedef void (*ld_reflex_preempt)(void* context,int prompt);
void ld_set_reflex_preempt(ld_engine*,ld_reflex_preempt,void* context);
void* ld_reflex_context(ld_engine*);
#ifdef __cplusplus
}
#endif
