#include "learndrive/dual_brain_c_api.h"
#include "learndrive/DualBrainEngine.hpp"
#include "learndrive/OnnxLanguageModel.hpp"
#include <cstring>
#include <stdexcept>
using namespace ld::cognition;
namespace {
ld::Telemetry telemetry(ld_telemetry t) {
    return {t.time, t.speed, t.pitch, t.acceleration, t.progress, bool(t.calibrated), bool(t.speed_valid),
            bool(t.pitch_valid), bool(t.braking), bool(t.hard_braking), bool(t.shock)};
}
struct CallbackSpeech final : SpeechDispatcher {
    ld_dual_callbacks callbacks;
    explicit CallbackSpeech(ld_dual_callbacks c) : callbacks(c) {}
    bool trySpeak(const SpeechCommand& c) noexcept override {
        try { return callbacks.try_speak(callbacks.context, c.text, c.code, c.priority, c.flush, c.distanceRelated) == 1; }
        catch (...) { return false; }
    }
    void stopDistanceAlerts() noexcept override {
        try { callbacks.stop_distance_alerts(callbacks.context); } catch (...) {}
    }
};
ContextualCognitionEngine::ModelFactory factory(const ld_dual_model_config* config) {
    if (!config) return []() -> std::unique_ptr<LanguageModel> { throw std::runtime_error("SLM not configured; reflex-only mode"); };
    if (!config->model_path || !config->tokenizer_path || config->provider < 0 || config->provider > 2)
        throw std::invalid_argument("invalid SLM configuration");
    OnnxModelOptions options;
    options.modelPath = config->model_path;
    options.provider = static_cast<ExecutionProvider>(config->provider);
    options.allowCpuFallback = config->allow_cpu_fallback != 0;
    if (config->coreml_cache_directory) options.coreMLCacheDirectory = config->coreml_cache_directory;
    const std::string tokenizerPath = config->tokenizer_path;
    return [options, tokenizerPath]() -> std::unique_ptr<LanguageModel> {
#if defined(LEARNDRIVE_WITH_ORT) && defined(LEARNDRIVE_WITH_HF_TOKENIZER)
        return std::make_unique<OnnxLanguageModel>(options, makeHuggingFaceTokenizer(tokenizerPath));
#else
        throw std::runtime_error("build native ORT and tokenizers-cpp support to enable SLM inference");
#endif
    };
}
}
struct ld_dual_engine {
    CallbackSpeech speech;
    DualBrainEngine brain;
    ld_dual_engine(ld_dual_callbacks c, const ld_dual_model_config* config)
        : speech(c), brain(speech, factory(config), c.clock_seconds ? SceneClock([c] { return c.clock_seconds(c.context); }) : SceneClock(monotonicSeconds)) {}
};
extern "C" {
ld_dual_engine* ld_dual_create(ld_dual_callbacks callbacks, const ld_dual_model_config* config) {
    if (!callbacks.try_speak || !callbacks.stop_distance_alerts) return nullptr;
    try { return new ld_dual_engine(callbacks, config); } catch (...) { return nullptr; }
}
void ld_dual_destroy(ld_dual_engine* engine) {
    if (!engine) return;
    engine->brain.stop(); engine->speech.stopDistanceAlerts(); delete engine;
}
int ld_dual_publish(ld_dual_engine* engine, const ld_dual_scene* input) {
    if (!engine || !input || input->count > maxHazards) return 0;
    try {
        SceneState s; s.sequence = input->sequence; s.captureTime = input->capture_time;
        s.telemetry = telemetry(input->telemetry); s.count = input->count;
        for (size_t i = 0; i < s.count; ++i) {
            const auto& h = input->hazards[i];
            if (h.type < LD_VEHICLE || h.type > LD_TAILGATER || h.lane < LD_EGO || h.lane > LD_LANE_UNKNOWN) return 0;
            s.hazards[i] = {h.id, static_cast<HazardType>(h.type), static_cast<Lane>(h.lane), h.distance_m,
                            h.tti_s, h.confidence, bool(h.in_trajectory), bool(h.decelerating),
                            h.lateral_distance_m, h.optical_relative_velocity_mps};
        }
        return engine->brain.publish(s);
    } catch (...) { return 0; }
}
int ld_dual_tick(ld_dual_engine* engine, ld_telemetry input, double now) {
    if (!engine || !std::isfinite(now) || now < 0) return -1;
    try {
        const auto result = engine->brain.tick(telemetry(input), now);
        return (result.command != ReflexCommand::none ? 1 : 0) | (result.suppressDistanceAlerts ? 2 : 0);
    } catch (...) { return -1; }
}
void ld_dual_reset(ld_dual_engine* engine) { if (engine) engine->brain.reset(); }
int ld_dual_status(ld_dual_engine* engine) { return engine?1:-1; }
size_t ld_dual_error(ld_dual_engine* engine, char* destination, size_t capacity) {
    try {
        const std::string error = engine ? "" : "invalid dual-brain handle";
        if (destination && capacity) {
            const size_t count = std::min(capacity - 1, error.size());
            std::memcpy(destination, error.data(), count); destination[count] = '\0';
        }
        return error.size() + 1;
    } catch (...) { if (destination && capacity) destination[0] = '\0'; return 0; }
}
}
