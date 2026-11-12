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
int ld_coaching_continuation_valid(ld_engine*,uint64_t sequence,int prompt,double now,uint64_t revision);
const char* ld_coaching_continuation_reason(ld_engine*,uint64_t sequence,int prompt,double now,uint64_t revision);
// 20 values: enabled,profile,voice,revision,mode,block,event,capture,age,
// historyCount,activeEvents,resolvedEvents,logicalAccepted,voiceSubmitted,
// playbackOnsets,onsetRejected,duplicates,deferred,speedTrend,ordinaryInterval.
// mode 0 SILENCE/1 COACH/2 WARN/3 DEFER; trend 0 unknown/1 steady/2 up/3 down.
// No images, coordinates, road names, routes or free-form speech are exported.
int ld_coaching_diagnostics(ld_engine*,double now,double* out,size_t capacity);
const char* ld_coaching_diagnostics_json(ld_engine*,double now);
// Version2 scalar interpreted context; Google-derived provider context is excluded.
const char* ld_coaching_context_json(ld_engine*);
void ld_coaching_junction_lesson(ld_engine*,int enabled);
void ld_coaching_automatic(ld_engine*,int enabled);
// Task-aware scheduling of automatic coaching (default on); off reproduces 0.7.1.
void ld_coaching_task_scheduling(ld_engine*,int enabled);
const char* ld_coaching_drive_json(ld_engine*);
// "You first" (0.8.5): the learner's measured responses to road users, per kind of situation
// (0 people ahead, 1 two-wheelers ahead, 2 vehicles ahead, 3 oncoming traffic). Each outcome is
// 1 slowed before the instruction (or it was not needed), 2 slowed after it, 3 no slowing seen.
// With enough outcomes over several drives, an instruction whose slowing the learner's measured
// speed shows is already done is shortened to what speed cannot show (level 1), then left unsaid
// and shown on screen (level 2). A learner who has not slowed is told in full, at the same moment;
// urgent instructions are never shortened or left unsaid. Speed only: an observation of slowing,
// never a grade of driving. Applies to, and learns from, the road camera only (never a test video
// or a simulation). The app stores it between drives.
void ld_learner_waiting(ld_engine*,int enabled);
void ld_learner_new_drive(ld_engine*);
// out[0] = drives with an outcome, then up to 30 outcomes, oldest first. Returns values written.
int ld_learner_outcomes(ld_engine*,int skill,int* out,size_t capacity);
int ld_learner_restore(ld_engine*,int skill,int drives,const int* outcomes,size_t count);
void ld_learner_forget(ld_engine*);
// Per kind of situation: wait level (0-2), recent and this-drive tallies; plus this drive's total.
const char* ld_learner_json(ld_engine*);
// The road user whose instruction is left unsaid right now (decided on a camera frame within the
// last 1.5 s): out[0] kind of situation (-1 none), out[1] 1 when the learner is measured slowing,
// out[2] seconds since it became due. Capacity >= 3.
int ld_learner_hold(ld_engine*,double now,double* out,size_t capacity);
// For the after-drive review: out[0..2] this drive's outcomes over all kinds (slowed before, after,
// no slowing seen), out[3..6] the level the record has now earned per kind (applied from the next
// drive; the level in force is fixed when a drive begins), out[7..10] recent outcomes kept per kind.
// Returns values written (11), 0 without room.
int ld_learner_summary(ld_engine*,int* out,size_t capacity);
const char* ld_coaching_task_json(ld_engine*);
int ld_coaching_task_speech_allowed(ld_engine*);
int ld_coaching_task_execution_quiet(ld_engine*);
// Diagnostic packed snapshot for matched local replay. Excludes provider data.
const char* ld_coaching_world_snapshot_json(ld_engine*);
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
// Composed utterances (ids >= 1000): fragment ids in speaking order; returns count.
int ld_prompt_fragments(int prompt,int* out,size_t capacity);
int ld_fragment_count(void);
const char* ld_fragment_text(int fragment);
// Road geometry (e.g. OpenStreetMap ways) for junction-ahead coaching without a
// destination. road_class: 0 motorway .. 9 track; lat_lon holds count pairs.
int ld_road_way(ld_engine*,int64_t way,int road_class,const int64_t* nodes,const double* lat_lon,size_t count);
void ld_road_clear(ld_engine*);
// A mapped speed bump (OSM traffic_calming) on a road node already added with ld_road_way.
void ld_road_bump(ld_engine*,int64_t node);
// The navigation provider's route line (lat,lon pairs), e.g. the Google route while guiding.
// Held in memory for this route only; never stored or exported. count 0 clears. Sharp bends
// on the route itself are coached once the maneuver positions below are known.
int ld_route_line(ld_engine*,const double* lat_lon,size_t count);
// Distances (m) from the car to each remaining maneuver on that route, nearest first, at `time`
// (engine clock). Bends at a maneuver are that turn and are never announced as bends.
void ld_route_maneuvers(ld_engine*,double time,const double* ahead_m,size_t count);
// Elevation along the current route line: lat, lon, metres triples (Google's Elevation API sampled
// along it). Memory only, replaced by the next call or route. Returns 1 when it covers the line.
int ld_route_elevation(ld_engine*,const double* lat_lon_elev,size_t count);
// out[4]: elevation covers [out0, out1] m along the line; the car is at out2 (-1 when not matched);
// out3 climbs/descents found. Tells the app when to fetch the next stretch.
void ld_route_elevation_cover(ld_engine*,double* out);
// Speed bumps learned on this phone from its motion sensor (felt on two separate passes before
// they are coached). Restore saved ones at start; read them back (5 values each: lat, lon,
// heading, times felt, times driven over without a jolt) to save between drives.
int ld_bump_restore(ld_engine*,double lat,double lon,double heading,int observations,int misses);
// Detail of the provider step last sent with ld_navigation_turn_update (same received_at):
// side for keep/fork (-1 left, +1 right, 0 none), roundabout exit (0 unknown), and the step
// after it (LD_MANEUVER_* and metres after this one; NaN/inf unknown). Memory only.
void ld_navigation_step_detail(ld_engine*,double received_at,int side,int roundabout_exit,int next_maneuver,double next_gap_m);
int ld_bumps_learned(ld_engine*,double* out,size_t capacity);
// Oldest bump-detector event not yet read, for diagnostics (no position). Returns 0 when none.
// out (capacity >= 12): time, accepted, reason, peak vertical m/s^2, rear-wheel vertical,
// axle gap s, pitch rate rad/s, roll rate rad/s, speed km/h, road background RMS, stored, located.
int ld_bump_event(ld_engine*,double* out,size_t capacity);
const char* ld_bump_reason(int reason);
// Map knowledge in use (capacity >= 10): route line loaded, matched, bends known, bend m,
// bend right, bump m, bump learned, learned bumps, learned bumps in use, bends on the route.
// out[0..9]: route line loaded, matched, bends known, bend m, bend right, bump m, bump learned,
// learned bumps, learned in use, route bends. With capacity >= 16 also: [10] map grade known,
// [11] grade here (%), [12] m to the next climb/descent (negative: on it), [13] its grade (%),
// [14] its length still ahead (m), [15] climbs/descents on the route.
int ld_road_knowledge(ld_engine*,double now,double* out,size_t capacity);
// Indian road practice (default on): horn at blind bends, junction entry and when passing.
void ld_coaching_horn_rules(ld_engine*,int enabled);
// Horizon row (fraction of image height) for filmed/replayed video, where the phone's own
// pitch does not describe the camera; NaN restores the measured camera pitch.
void ld_camera_horizon(ld_engine*,double fraction);
// Replays of filmed video: the camera's turn rate (rad/s, left positive) measured at `time`
// from the video itself; the phone uses its gyroscope.
void ld_camera_yaw(ld_engine*,double time,double radiansPerSecond);
// Lane model output (two-channel logits, drivable + lanes, model image size) for
// the frame most recently prepared; lane keeping on marked roads only.
int ld_lane_map(ld_engine*,uint64_t source,double time,const float* drivable,const float* lanes,int width,int height);
// out: valid, distance m, node id, road ends (T-junction). Capacity >= 4.
int ld_road_junction(ld_engine*,double now,double* out,size_t capacity);
// time, valid, bend (-1/0/+1), then five normalized x,y point pairs.
int ld_road_shape(ld_engine*,double* out,size_t capacity);
// All C ABI functions catch exceptions. Check error text after a failed operation.
const char* ld_error(ld_engine*);
const char* ld_decision_reason(ld_engine*);
// Packs x,y,width,height,score,COCO category per box. Returns boxes written.
size_t ld_boxes(ld_engine*,float* out,size_t capacity_floats);
int ld_preprocess(ld_engine*,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int width,int height,int rotation,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zero_point,int full_range,int save_gray);
// Optional zoomed detector pass (distant road users ahead): letterboxes a square crop
// of the SAME frame just prepared with ld_preprocess, centred on `horizon` (fraction of
// the upright height). Keeps the full-frame transform.
// The detector image as a mosaic: the road band (full width, from just above the horizon down)
// plus a 2x zoom of the far road around the horizon in the space letterboxing would waste; one
// inference sees road users about twice as far ahead. The horizon follows the camera's measured
// pitch at `time`. Detections are decoded as usual: zoom-band boxes are mapped back to frame
// geometry first, so distances are unchanged. Returns 2 with a zoom band, 1 for a plain
// letterbox (no room), 0 on error.
int ld_preprocess_mosaic(ld_engine*,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int width,int height,int rotation,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zero_point,int full_range,int save_gray,double time);
// Long range (0.8.3, the default mode 2): the mosaic holds TWO zoomed views of the road ahead,
// 2x and 4x the road band's scale, aimed where the road will be in ~2.5 s and ~5 s (route line
// when navigating, else the turn rate). Feed a high-resolution frame (1280 wide): the 4x view
// is then the camera's native pixels. Mode 1 restores the 0.8.0 layout (one centred 2x view).
void ld_mosaic_mode(ld_engine*,int mode);
// The layout ld_preprocess_mosaic would use at `time` (8 values: horizon, above, views, zoom x2,
// aim x2, smooth), and preparing with such a stored layout (replays re-decode cached detector
// output with exactly the layout it was inferred on).
int ld_mosaic_plan(ld_engine*,double time,float* out,size_t capacity);
int ld_preprocess_mosaic_plan(ld_engine*,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int width,int height,int rotation,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zero_point,int full_range,int save_gray,const float* plan,size_t count);
// The analysed image's optics, normalized to that image after any crop and rotation: focal
// lengths in image widths and image heights (per unit tangent) and the optical centre as
// fractions. The horizon, ground distance, apparent-size distance and lateral offsets of road
// users all use it. Invalid values restore the default (uncropped 4:3 frame: 0.72, 0.96, 0.5, 0.5).
void ld_camera_geometry(ld_engine*,double fx,double fy,double cx,double cy);
int ld_preprocess_zoom(ld_engine*,const uint8_t* y,size_t yn,int yr,int yp,const uint8_t* u,size_t un,int ur,int up,const uint8_t* v,size_t vn,int vr,int vp,int width,int height,int rotation,int size,void* tensor,size_t bytes,int dtype,int layout,float scale,int zero_point,int full_range,float horizon);
// Merges YOLO raw output of the full frame and of the zoom (both [84,N] channel-first)
// into [84,2N] full-frame model pixels for the usual decoding and NMS. Returns 2N or 0.
int ld_zoom_merge(ld_engine*,const float* full,const float* zoom,int candidates,float* out,size_t out_count);
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
