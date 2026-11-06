#include "learndrive/ContextualCognitionEngine.hpp"
#include <stdexcept>

namespace ld::cognition {
double monotonicSeconds() noexcept {
    return std::chrono::duration<double>(SteadyClock::now().time_since_epoch()).count();
}
const char* ContextualCognitionEngine::systemPrompt() noexcept {
    return "You are an expert driving instructor co-pilot. Analyze the JSON scene telemetry. "
           "Synthesize multiple hazards. Ignore low-risk parallel noise such as sidewalk pedestrians and distant signs. "
           "Output ONLY one JSON object with exactly these fields: "
           "{\"action_prompt\":\"<2-5 word audio prompt>\",\"priority\":2,\"suppress_other_alerts\":true}. "
           "Priority is 2-4; level 1 belongs exclusively to the independent emergency reflex. "
           "Choose action_prompt verbatim from: Hold lane and distance; Keep a safe gap; Slow down smoothly; "
           "Watch for pedestrians; Prepare for the bend; Observe the road sign. "
           "When a bump competes with a decelerating lead vehicle, favor Hold lane and distance and suppress other alerts. "
           "Never suggest swerving, acceleration or a numbered gear. Incline alone does not establish a gear. "
           "Do not invent measurements or interpret null as zero. No explanations, markdown or extra fields.";
}
ContextualCognitionEngine::ContextualCognitionEngine(ModelFactory factory, SceneClock clock, CognitionConfig config)
    : clock_(std::move(clock)), config_(config) {
    if (!factory || !clock_ || config.period < std::chrono::milliseconds(200) ||
        config.inferenceBudget <= std::chrono::milliseconds(0) || config.inferenceBudget > config.period)
        throw std::invalid_argument("invalid cognition factory, clock or scheduling budget");
    worker_ = std::thread(&ContextualCognitionEngine::run, this, std::move(factory));
}
ContextualCognitionEngine::~ContextualCognitionEngine() { stop(); }
void ContextualCognitionEngine::stop() {
    stopping_.store(true); cancel();
    if (worker_.joinable()) worker_.join();
}
uint64_t ContextualCognitionEngine::cancel() noexcept {
    const auto epoch = epoch_.fetch_add(1) + 1;
    wake_.notify_one(); return epoch;
}
bool ContextualCognitionEngine::submit(const SceneState& scene) noexcept {
    if (stopping_.load() || status_.load() == ModelStatus::failed) return false;
    const auto epoch = epoch_.load();
    std::unique_lock<std::mutex> lock(mailbox_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (pending_) ++overwritten_;
    pending_ = Work{scene, epoch};
    wake_.notify_one(); return true;
}
bool ContextualCognitionEngine::tryTake(ContextualDecision& decision) noexcept {
    std::unique_lock<std::mutex> lock(mailbox_, std::try_to_lock);
    if (!lock.owns_lock() || !result_) return false;
    const auto candidate = *result_; result_.reset();
    if (candidate.epoch != epoch_.load()) return false;
    decision = candidate; return true;
}
CognitionStats ContextualCognitionEngine::stats() const noexcept {
    return {completed_.load(), rejected_.load(), cancelled_.load(), errors_.load(), overwritten_.load()};
}
std::string ContextualCognitionEngine::lastError() const {
    std::lock_guard<std::mutex> lock(errorMutex_); return error_;
}
void ContextualCognitionEngine::error(const char* message) noexcept {
    ++errors_;
    try { std::lock_guard<std::mutex> lock(errorMutex_); error_ = message; } catch (...) {}
}
void ContextualCognitionEngine::run(ModelFactory factory) noexcept {
    try {
        // Tokenizer/model initialization and accelerator compilation never run
        // on the camera, IMU, audio or reflex thread.
        auto model = factory();
        if (!model) throw std::runtime_error("SLM factory returned no model");
        status_.store(ModelStatus::ready);
        auto nextStart = SteadyClock::now();
        while (!stopping_.load()) {
            Work work;
            {
                std::unique_lock<std::mutex> lock(mailbox_);
                wake_.wait(lock, [&] { return stopping_.load() || pending_.has_value(); });
                if (stopping_.load()) break;
                wake_.wait_until(lock, nextStart, [&] { return stopping_.load(); });
                if (stopping_.load()) break;
                work = *pending_; pending_.reset();
            }
            if (work.epoch != epoch_.load()) { ++cancelled_; continue; }
            const double now = clock_();
            if (!fresh(now, work.scene.captureTime) || hardBraking(work.scene.telemetry, now)) { ++rejected_; continue; }
            const auto start = SteadyClock::now();
            nextStart = start + config_.period;
            Cancellation token{epoch_, stopping_, work.epoch, start + config_.inferenceBudget};
            try {
                const auto response = model->generate(systemPrompt(), serializeScene(work.scene, now), token);
                if (token.cancelled()) { ++cancelled_; continue; }
                ContextualDecision decision;
                if (!parseResponse(response, decision) || !actionSupported(decision.action, work.scene) ||
                    !fresh(clock_(), work.scene.captureTime)) { ++rejected_; continue; }
                decision.captureTime = work.scene.captureTime; decision.epoch = work.epoch;
                std::lock_guard<std::mutex> lock(mailbox_);
                if (!token.cancelled()) { result_ = decision; ++completed_; }
                else ++cancelled_;
            } catch (const std::exception& ex) {
                if (token.cancelled()) ++cancelled_; else error(ex.what());
            } catch (...) { error("unknown SLM inference failure"); }
        }
        status_.store(ModelStatus::stopped);
    } catch (const std::exception& ex) { error(ex.what()); status_.store(ModelStatus::failed); }
    catch (...) { error("unknown SLM initialization failure"); status_.store(ModelStatus::failed); }
}
} // namespace ld::cognition
