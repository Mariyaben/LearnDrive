#pragma once
#include "c_api.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ld_dual_engine ld_dual_engine;
enum ld_hazard_type { LD_VEHICLE, LD_BUS, LD_PEDESTRIAN, LD_POTHOLE, LD_SIGN, LD_BEND, LD_UNKNOWN, LD_TAILGATER };
enum ld_lane { LD_EGO, LD_ADJACENT, LD_SIDEWALK, LD_LANE_UNKNOWN };
typedef struct {
    uint64_t id;
    int type, lane;
    double distance_m, tti_s, confidence;
    int in_trajectory, decelerating;
    double lateral_distance_m, optical_relative_velocity_mps;
} ld_scene_hazard;
typedef struct {
    uint64_t sequence;
    double capture_time;
    ld_telemetry telemetry;
    size_t count;
    ld_scene_hazard hazards[64];
} ld_dual_scene;
typedef struct {
    void* context;
    // Called on the serial 100 Hz rules thread. Return 1 only if accepted.
    // Copy text/code if needed after return. Must not block or throw. A flush
    // atomically cancels both current and queued audio before cached playback.
    int (*try_speak)(void*, const char* text, const char* code, int priority, int flush, int distance_related);
    void (*stop_distance_alerts)(void*);
    // Must be thread-safe; same monotonic epoch as scene/telemetry/tick timestamps.
    // Null selects std::chrono::steady_clock seconds.
    double (*clock_seconds)(void*);
} ld_dual_callbacks;
typedef struct {
    const char* model_path;
    const char* tokenizer_path;
    int provider; // 0 CPU, 1 CoreML, 2 NNAPI. LiteRT is not an ORT provider.
    int allow_cpu_fallback;
    const char* coreml_cache_directory;
} ld_dual_model_config;
// Native mobile integration seam; no Java/Swift objects enter the SLM worker.
// Null model config creates the full deterministic reflex/tactical engine.
// Model configuration is retained only for explicit post-drive summary work.
// Phi-3 template/token IDs are used by this convenience ABI. Other model
// families use OnnxLanguageModel's explicit C++ configuration.
ld_dual_engine* ld_dual_create(ld_dual_callbacks, const ld_dual_model_config*);
// Stop producers/ticks first; destroy on a lifecycle thread, not camera/IMU.
void ld_dual_destroy(ld_dual_engine*);
int ld_dual_publish(ld_dual_engine*, const ld_dual_scene*);
// Return bit 0 = immediate reflex alert, bit 1 = distance alerts suppressed, -1 = invalid call.
// All tick/reset/minor calls must be serialized; invoke tick every 10 ms.
int ld_dual_tick(ld_dual_engine*, ld_telemetry, double now);
void ld_dual_reset(ld_dual_engine*);
int ld_dual_status(ld_dual_engine*); // 1 deterministic engine ready; -1 invalid handle.
// Copies NUL-terminated diagnostic text; return required bytes including NUL.
size_t ld_dual_error(ld_dual_engine*, char* destination, size_t capacity);
// Snapshot the existing vision adapter while holding the platform's NativeCore
// lock. Preserves unknown metric distance and per-object expansion confidence.
int ld_dual_read_vision(ld_engine*, ld_dual_scene*);
#ifdef __cplusplus
}
#endif
