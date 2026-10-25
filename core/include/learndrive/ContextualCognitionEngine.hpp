#pragma once
#include "PriorityArbiter.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace ld::cognition {
using SteadyClock = std::chrono::steady_clock;
using SceneClock = std::function<double()>;
double monotonicSeconds() noexcept;
struct Cancellation {
    const std::atomic<uint64_t>& epoch;
    const std::atomic<bool>& stopping;
    uint64_t expected;
    SteadyClock::time_point deadline;
    bool cancelled() const noexcept {
        return stopping.load() || epoch.load() != expected || SteadyClock::now() >= deadline;
    }
};
class LanguageModel {
public:
    virtual ~LanguageModel() = default;
    // Called only on the SLM worker. Must honor cancellation during decoding.
    virtual std::string generate(const std::string& systemPrompt, const std::string& sceneJson,
                                 const Cancellation& cancel) = 0;
};
struct CognitionConfig {
    std::chrono::milliseconds period{200};
    std::chrono::milliseconds inferenceBudget{150};
};
enum class ModelStatus { starting, ready, failed, stopped };
struct CognitionStats { uint64_t completed{}, rejected{}, cancelled{}, errors{}, overwritten{}; };
class ContextualCognitionEngine {
public:
    using ModelFactory = std::function<std::unique_ptr<LanguageModel>()>;
    explicit ContextualCognitionEngine(ModelFactory factory, SceneClock clock = monotonicSeconds,
                                       CognitionConfig config = {});
    ~ContextualCognitionEngine();
    ContextualCognitionEngine(const ContextualCognitionEngine&) = delete;
    ContextualCognitionEngine& operator=(const ContextualCognitionEngine&) = delete;
    // Nonblocking bounded mailboxes. Drop a submission on lock contention;
    // otherwise replace the single pending snapshot (never queue stale scenes).
    bool submit(const SceneState& scene) noexcept;
    bool tryTake(ContextualDecision& decision) noexcept;
    uint64_t cancel() noexcept;
    uint64_t epoch() const noexcept { return epoch_.load(); }
    void stop(); // Join from lifecycle/control thread, never camera/reflex thread.
    ModelStatus status() const noexcept { return status_.load(); }
    CognitionStats stats() const noexcept;
    std::string lastError() const; // Diagnostic/control thread only.
    static const char* systemPrompt() noexcept;
private:
    struct Work { SceneState scene; uint64_t epoch; };
    void run(ModelFactory factory) noexcept;
    void error(const char* message) noexcept;
    SceneClock clock_;
    CognitionConfig config_;
    std::atomic<uint64_t> epoch_{1};
    std::atomic<bool> stopping_{false};
    std::atomic<ModelStatus> status_{ModelStatus::starting};
    std::atomic<uint64_t> completed_{0}, rejected_{0}, cancelled_{0}, errors_{0}, overwritten_{0};
    mutable std::mutex mailbox_, errorMutex_;
    std::condition_variable wake_;
    std::optional<Work> pending_;
    std::optional<ContextualDecision> result_;
    std::string error_;
    std::thread worker_;
};
} // namespace ld::cognition
