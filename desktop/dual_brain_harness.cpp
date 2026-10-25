#include "learndrive/DualBrainEngine.hpp"
#include "learndrive/OnnxLanguageModel.hpp"
#include <iostream>
#include <stdexcept>
using namespace ld;
using namespace ld::cognition;
// Deterministic fixture verifies orchestration, not model reasoning accuracy.
class FixtureModel final : public LanguageModel {
public:
    std::string generate(const std::string&, const std::string& json, const Cancellation&) override {
        if (json.find("\"incline_deg\":8.5") == std::string::npos || json.find("pothole") == std::string::npos ||
            json.find("bus") == std::string::npos || json.find("\"decelerating\":true") == std::string::npos)
            throw std::runtime_error("multi-hazard inputs did not reach evaluator");
        return "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2,\"suppress_other_alerts\":true}";
    }
};
class ConsoleSpeech final : public SpeechDispatcher {
public:
    Action tactical = Action::none;
    bool emergency{}, flush{};
    bool trySpeak(const SpeechCommand& c) noexcept override {
        flush = c.flush;
        emergency |= c.priority == 0;
        if (std::string_view(c.text) == actionText(Action::holdLaneAndDistance)) tactical = Action::holdLaneAndDistance;
        std::cout << "L" << c.priority << (c.flush ? " FLUSH: " : ": ") << c.text << '\n';
        return true;
    }
    void stopDistanceAlerts() noexcept override { std::cout << "STOP distance speech\n"; }
};
int main(int argc, char** argv) {
    (void)argv; // Used only when the optional live-model adapter is built.
    try {
        bool live = argc == 3;
        if (argc != 1 && !live) throw std::runtime_error("usage: dual_brain_harness [model.onnx tokenizer.json]");
        ContextualCognitionEngine::ModelFactory factory = [] { return std::make_unique<FixtureModel>(); };
        if (live) {
#if defined(LEARNDRIVE_WITH_ORT) && defined(LEARNDRIVE_WITH_HF_TOKENIZER)
            OnnxModelOptions options; options.modelPath = argv[1];
            const std::string tokenizerPath = argv[2];
            factory = [options, tokenizerPath] { return std::make_unique<OnnxLanguageModel>(options, makeHuggingFaceTokenizer(tokenizerPath)); };
#else
            throw std::runtime_error("live inference requires ORT and tokenizers-cpp build options; fixture cannot validate SLM reasoning");
#endif
        }
        ConsoleSpeech audio; DualBrainEngine brain(audio, factory);
        std::cout << "DETERMINISTIC tactical engine; language models are post-drive only\n";
        SceneState s; s.count = 3; s.telemetry.speedKmh = 45; s.telemetry.pitchDegrees = 8.5;
        s.telemetry.speedValid = s.telemetry.pitchValid = true;
        s.hazards[0] = {1, HazardType::bus, Lane::ego, 12, 3.8, .9, true, true};
        s.hazards[1] = {2, HazardType::pothole, Lane::ego, 5, 2.4, .9, true, false};
        s.hazards[2] = {3, HazardType::pedestrian, Lane::sidewalk, 18, 8, .9, false, false};
        // Separate latest snapshots at ~30 Hz and rules at ~100 Hz. Real SLM
        // responses retain their original capture timestamp and 150 ms budget.
        auto nextTick = SteadyClock::now();
        for (int i = 0; i < 300 && audio.tactical == Action::none; ++i) {
            const double now = monotonicSeconds();
            s.telemetry.time = now;
            if (i % 3 == 0) { s.captureTime = now; ++s.sequence; brain.publish(s); }
            brain.tick(s.telemetry, now);
            nextTick += std::chrono::milliseconds(10); std::this_thread::sleep_until(nextTick);
        }
        if (audio.tactical != Action::holdLaneAndDistance || !audio.flush)
            throw std::runtime_error("no timely primary tactical action; live model accuracy/latency has not met the scenario");
        if (brain.minor(Action::slowSmoothly, monotonicSeconds())) throw std::runtime_error("minor warning not suppressed");
        // Exact user example: TTI 1.2 s is a hard emergency regardless of incline.
        s.captureTime = s.telemetry.time = monotonicSeconds(); s.hazards[1].ttiSeconds = 1.2;
        brain.publish(s); brain.tick(s.telemetry, s.captureTime);
        if (!audio.emergency || !audio.flush) throw std::runtime_error("reflex failed to preempt speech");
        brain.stop();
        if(live)std::cout<<"Post-drive summary: "<<brain.summarizeAfterDrive(serializeScene(s,s.captureTime)).get()<<'\n';
        std::cout << "PASS: tactical suppression, hard emergency override, 100 Hz/10 Hz scheduling\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr << "FAIL: " << ex.what() << '\n'; return 1; }
}
