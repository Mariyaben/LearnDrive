#include "learndrive/DualBrainEngine.hpp"
#include "learndrive/ContextualPolicyEngine.hpp"
#include "learndrive/dual_brain_c_api.h"
#include <iostream>
#include <functional>
#include <stdexcept>
#include <vector>
#include <cstring>
using namespace ld;
using namespace ld::cognition;
using namespace std::chrono_literals;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
static SceneState scene(double now = 10) {
    SceneState s; s.captureTime = now; s.sequence = 1;
    s.telemetry.time = now; s.telemetry.speedKmh = 45; s.telemetry.pitchDegrees = 8.5;
    s.telemetry.speedValid = s.telemetry.pitchValid = true;
    s.count = 3;
    s.hazards[0] = {1, HazardType::bus, Lane::ego, 12, 3.8, .9, true, true};
    s.hazards[1] = {2, HazardType::pothole, Lane::ego, 5, 2.4, .9, true, false};
    s.hazards[2] = {3, HazardType::pedestrian, Lane::sidewalk, 18, 8, .9, false, false};
    return s;
}
static constexpr const char* answer = "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2,\"suppress_other_alerts\":true}";
struct Audio : SpeechDispatcher {
    std::vector<std::string> spoken;
    bool available = true, stopped = false, flushed = false;
    int priority = 0;
    bool trySpeak(const SpeechCommand& c) noexcept override {
        if (!available) return false;
        try { spoken.emplace_back(c.text); } catch (...) { return false; }
        flushed = c.flush; priority = c.priority; return true;
    }
    void stopDistanceAlerts() noexcept override { stopped = true; }
};
struct Model : LanguageModel {
    std::atomic<int>& calls;
    std::chrono::milliseconds delay;
    std::string output;
    bool throws{};
    Model(std::atomic<int>& c, std::chrono::milliseconds d = 0ms, std::string o = answer) : calls(c), delay(d), output(std::move(o)) {}
    std::string generate(const std::string&, const std::string& json, const Cancellation& cancel) override {
        ++calls;
        check(json.find("\"decelerating\":true") != std::string::npos, "lead vehicle not serialized");
        const auto end = SteadyClock::now() + delay;
        while (SteadyClock::now() < end && !cancel.cancelled()) std::this_thread::sleep_for(1ms);
        if (throws) throw std::runtime_error("test inference error");
        return output; // Deliberately returns even after cancellation; engine must gate it.
    }
};
static void until(const std::function<bool()>& predicate) {
    auto deadline = SteadyClock::now() + 2s;
    do { if (predicate()) return; std::this_thread::sleep_for(1ms); } while (SteadyClock::now() < deadline);
    throw std::runtime_error("asynchronous condition timed out");
}
int main() {
    int passed = 0, failed = 0;
    auto test = [&](const char* name, auto fn) {
        try { fn(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    test("strict TTI threshold and capture-age compensation", [] {
        ReflexEngine r; auto s = scene(); s.hazards[1].ttiSeconds = 2;
        check(r.evaluate(s, 10).command == ReflexCommand::none, "2 seconds triggered strict threshold");
        check(r.evaluate(s, 10.01).command == ReflexCommand::BRAKE_NOW, "age not subtracted");
        s.hazards[1].ttiSeconds = 1.2;
        check(r.evaluate(s, 10).priority == 0, "exact user scene must preempt SLM");
    });
    test("pedestrian within five meters requires active trajectory", [] {
        ReflexEngine r; auto s = scene(); auto& p = s.hazards[2]; p.distanceM = 5; p.ttiSeconds = infinity;
        check(r.evaluate(s, 10).command == ReflexCommand::none, "sidewalk false positive");
        p.inTrajectory = true;
        check(r.evaluate(s, 10).command == ReflexCommand::BRAKE_NOW, "crossing pedestrian missed");
        p.distanceM = 5.01; check(r.evaluate(s, 10).command == ReflexCommand::none, "distance threshold widened");
    });
    test("hard braking suppresses routine distance speech but retains critical escalation", [] {
        ReflexEngine r; auto s = scene(); s.hazards[1].ttiSeconds = .5;
        s.telemetry.longitudinalAcceleration = -3.01;
        check(r.evaluate(s, 10).suppressDistanceAlerts && r.evaluate(s, 10).command == ReflexCommand::BRAKE_HARDER, "critical braking escalation missing");
        s.hazards[1].ttiSeconds = 1.5;
        check(r.evaluate(s, 10).suppressDistanceAlerts && r.evaluate(s, 10).command == ReflexCommand::none, "routine alert not suppressed while braking");
        s.telemetry.longitudinalAcceleration = -3;
        s.hazards[1].ttiSeconds = .5;
        check(r.evaluate(s, 10).command == ReflexCommand::BRAKE_HARDER, "moderate braking did not escalate");
        s.telemetry.longitudinalAcceleration = 4;
        check(!r.evaluate(s, 10).suppressDistanceAlerts, "acceleration treated as braking");
        s.telemetry.hardBraking = true; s.telemetry.time = 9;
        check(!r.evaluate(s, 10).suppressDistanceAlerts, "stale brake flag muted emergency");
    });
    test("invalid, low confidence, stale and future detections rejected", [] {
        ReflexEngine r; auto s = scene(); s.hazards[1].ttiSeconds = .1;
        check(r.evaluate(s, 10.36).command == ReflexCommand::none, "stale frame accepted");
        check(r.evaluate(s, 9.9).command == ReflexCommand::none, "future frame accepted");
        s.hazards[1].confidence = std::numeric_limits<double>::quiet_NaN();
        check(r.evaluate(s, 10).command == ReflexCommand::none, "NaN confidence accepted");
        s.hazards[1].confidence = .9; s.hazards[1].ttiSeconds = -1;
        check(r.evaluate(s, 10).command == ReflexCommand::none, "negative TTI accepted");
        s.hazards[1].ttiSeconds = 0;
        check(r.evaluate(s, 10).command == ReflexCommand::EMERGENCY_STOP, "zero TTI ignored");
    });
    test("serializer preserves unknowns and bounded urgent ordering", [] {
        auto s = scene(); s.hazards[0].distanceM = infinity; s.hazards[0].ttiSeconds = std::numeric_limits<double>::quiet_NaN();
        const auto json = serializeScene(s, 10);
        check(json.find("\"incline_deg\":8.5") != std::string::npos, "incline missing");
        check(json.find("\"distance_m\":null") != std::string::npos && json.find("\"tti_s\":null") != std::string::npos, "unknown mapped to number");
        check(json.find("nan") == std::string::npos && json.find("inf") == std::string::npos, "invalid JSON float");
        s.count = maxHazards;
        for (auto& h : s.hazards) h = {4, HazardType::sign, Lane::adjacent, 50, infinity, .9, false, false};
        s.hazards.back() = {5, HazardType::bus, Lane::ego, 10, 1, .9, true, true};
        auto bounded = serializeScene(s, 10);
        check(bounded.find("bus") < bounded.find("sign"), "urgent object truncated");
        check(bounded.find("\"omitted\":52") != std::string::npos, "hazard budget not enforced");
    });
    test("strict schema, permitted phrases, reserved emergency priority", [] {
        ContextualDecision d; check(parseResponse(answer, d), "valid decision rejected");
        check(parseResponse(" { \"priority\": 3, \"suppress_other_alerts\": false, \"action_prompt\": \"Keep a safe gap\" } ", d), "reordered fields rejected");
        const std::vector<std::string> invalid{
            std::string(answer) + " explanation", "```json\n" + std::string(answer) + "\n```",
            "{\"action_prompt\":\"Swerve left now\",\"priority\":2,\"suppress_other_alerts\":true}",
            "{\"action_prompt\":\"Hold lane and distance\",\"priority\":1,\"suppress_other_alerts\":true}",
            "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2.0,\"suppress_other_alerts\":true}",
            "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2,\"priority\":3}",
            "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2,\"suppress_other_alerts\":1}",
            "{\"action_prompt\":\"Hold lane and distance\",\"priority\":2,\"suppress_other_alerts\":true,\"extra\":1}"};
        for (const auto& input : invalid) check(!parseResponse(input, d), "invalid response accepted");
    });
    test("deterministic policy resolves lead vehicle over pothole without language model", [] {
        ContextualPolicyEngine policy; auto s = scene();
        const auto decision = policy.evaluate(s, 10);
        check(decision.action == Action::holdLaneAndDistance && decision.priority == 2 && decision.suppressOtherAlerts,
              "multi-hazard policy did not hold lane and distance");
        check(std::string(decision.reason).find("pothole") != std::string::npos, "policy reason missing evidence");
    });
    test("deterministic policy ignores sidewalk noise and ranks measured hazards", [] {
        ContextualPolicyEngine policy; auto s = scene(); s.count = 1; s.hazards[0] = s.hazards[2];
        check(policy.evaluate(s, 10).action == Action::none, "sidewalk pedestrian became tactical action");
        s.hazards[0] = {4, HazardType::pedestrian, Lane::ego, 8, 4, .9, true, false};
        check(policy.evaluate(s, 10).action == Action::watchPedestrians, "path pedestrian not prioritized");
        s.hazards[0] = {5, HazardType::bend, Lane::ego, infinity, infinity, .9, true, false};
        check(policy.evaluate(s, 10).action == Action::prepareBend, "bend not selected");
    });
    test("embedded policy retries unaccepted audio and commits cooldown only on acceptance", [] {
        EmbeddedDualBrain brain; auto s = scene();
        auto first = brain.evaluate(s, 10);
        check(first.action == Action::holdLaneAndDistance, "first contextual action missing");
        check(brain.evaluate(s, 10.01).action == Action::holdLaneAndDistance, "unaccepted action was consumed");
        brain.accepted(first.action, false, 10.01);
        s.sequence++; s.captureTime = s.telemetry.time = 10.21;
        check(brain.evaluate(s, 10.21).action == Action::none, "accepted action bypassed cooldown");
        s.sequence++; s.captureTime = s.telemetry.time = 20.01;
        check(brain.evaluate(s, 20.01).action == Action::holdLaneAndDistance, "ten second dedup did not rearm");
    });
    test("global cooldown, ten second dedup, audio refusal does not commit", [] {
        Audio audio; PriorityArbiter arb(audio); auto s = scene(); ContextualDecision d;
        check(parseResponse(answer, d), "fixture"); d.captureTime = 10; d.epoch = 1;
        audio.available = false; check(!arb.contextual(d, s, 1, 10), "unavailable output accepted");
        audio.available = true; check(arb.contextual(d, s, 1, 10), "audio refusal consumed cooldown");
        d.action = Action::keepSafeGap; d.captureTime = s.captureTime = 13.49;
        check(!arb.contextual(d, s, 1, 13.49), "global cooldown bypassed");
        d.captureTime = s.captureTime = 13.5; check(arb.contextual(d, s, 1, 13.5), "cooldown boundary");
        d.action = Action::holdLaneAndDistance; d.captureTime = s.captureTime = 19.99;
        check(!arb.contextual(d, s, 1, 19.99), "dedup bypassed");
        d.captureTime = s.captureTime = 20; check(arb.contextual(d, s, 1, 20), "dedup boundary");
    });
    test("reflex flushes ongoing speech and does not stutter at 100 Hz", [] {
        Audio audio; PriorityArbiter arb(audio); auto s = scene(); ContextualDecision d;
        parseResponse(answer, d); d.captureTime = 10; d.epoch = 1;
        check(arb.contextual(d, s, 1, 10), "tactical speech not accepted");
        arb.reflex({ReflexCommand::BRAKE_NOW, false, 2, 1}, 10.01);
        check(audio.flushed && audio.priority == 0 && audio.spoken.back() == "Brake now", "reflex did not flush");
        for (int i = 2; i < 30; ++i) arb.reflex({ReflexCommand::BRAKE_NOW, false, 2, 1}, 10 + i * .01);
        check(audio.spoken.size() == 2, "100 Hz emergency restarted audio");
        arb.reflex({ReflexCommand::none, true, 0, 0}, 10.3);
        check(audio.stopped, "braking failed to cancel active distance speech");
    });
    test("old epochs, stale results and unsupported advice never speak", [] {
        Audio audio; PriorityArbiter arb(audio); auto s = scene(); ContextualDecision d;
        parseResponse(answer, d); d.captureTime = 10; d.epoch = 1;
        check(!arb.contextual(d, s, 2, 10), "pre-emergency result accepted");
        check(!arb.contextual(d, s, 1, 10.36), "expired response accepted");
        d.action = Action::watchPedestrians;
        check(!arb.contextual(d, s, 1, 10), "sidewalk pedestrian caused guidance");
    });
    test("relevant signs use the same ten second dedup", [] {
        Audio audio; PriorityArbiter arb(audio); auto s = scene(); s.count = 1;
        s.hazards[0] = {99, HazardType::sign, Lane::ego, 20, infinity, .9, false, false};
        ContextualDecision d{Action::observeSign, 4, false, 10, 1};
        check(arb.contextual(d, s, 1, 10), "relevant sign ignored");
        s.captureTime = d.captureTime = 15;
        check(!arb.contextual(d, s, 1, 15), "sign duplicated inside lock");
        s.captureTime = d.captureTime = 20;
        check(arb.contextual(d, s, 1, 20), "sign never rearmed");
        s.hazards[0].distanceM = 100;
        check(!actionSupported(Action::observeSign, s), "distant sign accepted");
    });
    test("worker serializes scene and suppresses competing minor warning", [] {
        std::atomic<int> calls{0}; Audio audio;
        DualBrainEngine brain(audio, [&] { return std::make_unique<Model>(calls); }, [] { return 10.0; });
        auto s = scene(); check(brain.publish(s), "publish failed"); brain.tick(s.telemetry, 10);
        check(calls==0,"driving tick invoked a language model");
        brain.tick(s.telemetry, 10.01);
        check(audio.spoken.size() == 1 && audio.spoken[0] == "Hold lane and distance", "primary tactical action missing");
        check(!brain.minor(Action::slowSmoothly, 10.02), "minor pothole warning not suppressed");
    });
    test("exact 1.2 second pothole scene bypasses SLM completely", [] {
        std::atomic<int> calls{0}; Audio audio;
        DualBrainEngine brain(audio, [&] { return std::make_unique<Model>(calls); }, [] { return 10.0; });
        auto s = scene(); s.hazards[1].ttiSeconds = 1.2; brain.publish(s);
        check(brain.tick(s.telemetry, 10).command == ReflexCommand::BRAKE_NOW, "no hard override");
        brain.stop(); check(calls == 0 && audio.spoken.back() == "Brake now", "SLM ran during emergency");
    });
    test("driving never starts the optional summary model", [] {
        std::atomic<int> calls{0}; Audio audio;
        DualBrainEngine brain(audio, [&] { return std::make_unique<Model>(calls, 100ms); }, [] { return 10.0; });
        auto s = scene(); brain.publish(s);brain.tick(s.telemetry,10);
        s.captureTime = 10.01; s.hazards[1].ttiSeconds = .5; brain.publish(s); brain.tick(s.telemetry, 10.01);
        check(calls==0,"optional model executed during drive");
        check(audio.spoken.back()=="Emergency stop","emergency did not win");
    });
    test("post-drive summaries cannot load or speak during a drive", [] {
        std::atomic<int> calls{0},loads{0};Audio audio;
        DualBrainEngine brain(audio,[&]{++loads;return std::make_unique<Model>(calls);},[]{return 10.;});
        bool rejected=false;try{brain.summarizeAfterDrive("{}");}catch(const std::logic_error&){rejected=true;}
        check(rejected&&calls==0&&loads==0,"summary started while driving");
        brain.stop();auto result=brain.summarizeAfterDrive(serializeScene(scene(),10)).get();
        check(calls==1&&loads==1&&!result.empty()&&audio.spoken.empty(),"summary text reached audio dispatcher");
    });
    test("timeout and malformed inference fail without blocking the reflex", [] {
        std::atomic<int> calls{0};
        ContextualCognitionEngine engine([&] { return std::make_unique<Model>(calls, 100ms); }, [] { return 10.0; }, {200ms, 20ms});
        check(engine.submit(scene()), "submit failed");
        until([&] { return engine.stats().cancelled > 0; });
        ContextualDecision d; check(!engine.tryTake(d), "timed out answer published");
        ContextualCognitionEngine malformed([&] { return std::make_unique<Model>(calls, 0ms, "not json"); }, [] { return 10.0; });
        malformed.submit(scene()); until([&] { return malformed.stats().rejected > 0; });
        check(!malformed.tryTake(d), "malformed answer published");
    });
    test("model initialization failure leaves reflex available", [] {
        Audio audio;
        DualBrainEngine brain(audio, []() -> std::unique_ptr<LanguageModel> { throw std::runtime_error("missing model"); });
        auto s = scene(); s.hazards[1].ttiSeconds = 1; brain.publish(s); brain.tick(s.telemetry, 10);
        check(audio.spoken.size() == 1 && audio.spoken[0] == "Emergency stop", "model failure disabled reflex");
        brain.stop();bool failed=false;try{brain.summarizeAfterDrive("{}").get();}catch(...){failed=true;}
        check(failed,"post-drive model load failure was hidden");
    });
    test("latest-only mailbox bounded and launches no faster than 5 Hz", [] {
        std::atomic<int> calls{0};
        ContextualCognitionEngine engine([&] { return std::make_unique<Model>(calls, 40ms); }, [] { return 10.0; });
        engine.submit(scene()); until([&] { return calls == 1; });
        for (int i = 0; i < 500; ++i) engine.submit(scene());
        std::this_thread::sleep_for(80ms);
        check(calls == 1 && engine.stats().overwritten > 0, "unbounded queue or scheduling burst");
        until([&] { return calls == 2; });
        engine.stop(); check(calls == 2, "queued every scene");
    });
    test("newest pending scene wins while inference is busy", [] {
        std::atomic<int> calls{0};
        ContextualCognitionEngine engine([&] { return std::make_unique<Model>(calls, 40ms); }, [] { return 10.1; });
        engine.submit(scene(10)); until([&] { return calls == 1; });
        auto submit = [&](const SceneState& s) { until([&] { return engine.submit(s); }); };
        submit(scene(10.01)); submit(scene(10.02)); submit(scene(10.1));
        until([&] { return engine.stats().completed == 2; });
        ContextualDecision result;
        until([&] { return engine.tryTake(result); });
        check(result.captureTime == 10.1 && calls == 2, "old pending scene was evaluated");
    });
    test("C ABI validates input and supports reflex without model assets", [] {
        struct State { int calls = 0, priority = 0; bool flush = false, stopped = false; } state;
        ld_dual_callbacks callbacks{}; callbacks.context = &state;
        callbacks.try_speak = [](void* ptr, const char*, const char* code, int priority, int flush, int) {
            auto& state = *static_cast<State*>(ptr); ++state.calls; state.priority = priority; state.flush = flush;
            return std::strcmp(code, "BRAKE_NOW") == 0||std::strcmp(code,"EMERGENCY_STOP")==0 ? 1 : 0;
        };
        callbacks.stop_distance_alerts = [](void* ptr) { static_cast<State*>(ptr)->stopped = true; };
        auto engine = std::unique_ptr<ld_dual_engine, decltype(&ld_dual_destroy)>(ld_dual_create(callbacks, nullptr), ld_dual_destroy);
        check(bool(engine), "C ABI allocation failed");
        check(ld_dual_status(engine.get())==1,"deterministic engine should need no SLM assets");
        char error[128]{};check(ld_dual_error(engine.get(),error,sizeof(error))==1,"unexpected model dependency");
        ld_dual_scene s{}; s.capture_time = 10; s.count = 1;
        s.hazards[0] = {1, LD_BUS, LD_EGO, 10, 1, .9, 1, 1, 1.0, 0};
        check(ld_dual_publish(engine.get(), &s), "C scene rejected");
        ld_telemetry t{}; t.time = 10;
        check(ld_dual_tick(engine.get(), t, 10) == 1 && state.calls == 1 && state.priority == 0 && state.flush, "C ABI preemption failed");
        s.capture_time=10.01;s.hazards[0].tti_s=1.5;check(ld_dual_publish(engine.get(),&s),"updated C scene rejected");
        t.acceleration = -4; t.time = 10.01;
        check(ld_dual_tick(engine.get(), t, 10.01) == 2 && state.stopped, "C ABI braking failed");
        s.count = 65; check(!ld_dual_publish(engine.get(), &s), "oversized C scene accepted");
        check(ld_dual_tick(nullptr, t, 10) == -1, "null engine accepted");
        check(!ld_dual_create({}, nullptr), "missing audio callbacks accepted");
    });
    test("current vision adapter preserves per-object TTI and unknown ranging", [] {
        auto e = std::unique_ptr<ld_engine, decltype(&ld_destroy)>(ld_create(), ld_destroy);
        check(bool(e), "vision engine allocation failed");
        ld_dual_scene output{}; check(!ld_dual_read_vision(e.get(), &output), "uninitialized vision accepted");
        uint8_t y[4]{128,128,128,128}, uv[1]{128}; std::vector<float> tensor(3 * 320 * 320);
        check(ld_preprocess(e.get(), y, 4, 2, 1, uv, 1, 1, 1, uv, 1, 1, 1, 2, 2, 0, 320,
              tensor.data(), tensor.size() * 4, 0, 1, 1, 0, 1, 1), "preprocess failed");
        float detection[84]{}; detection[0] = detection[1] = 160; detection[2] = detection[3] = 100; detection[9] = .9f;
        float mask[8]{-2,-2,-2,-2,2,2,2,2}; double hazard[3]{};
        check(ld_vision(e.get(), 10, detection, 84, 84, 1, 1, 0, mask, mask, 2, 2, hazard), "vision failed");
        check(ld_dual_read_vision(e.get(), &output) && output.count == 1 && output.capture_time == 10, "scene adapter lost capture");
        check(output.hazards[0].type == LD_BUS && output.hazards[0].in_trajectory, "scene adapter lost category/path");
        check(std::isinf(output.hazards[0].distance_m) && std::isinf(output.hazards[0].tti_s), "metric range or motion invented");
        check(output.hazards[0].confidence > .89, "contextual detection confidence lost");
        check(!ld_vision(e.get(), 11, detection, 84, 10, 1, 1, 0, mask, mask, 2, 2, hazard), "invalid model contract accepted");
        check(!ld_dual_read_vision(e.get(), &output), "failed vision retained scene");
    });
    test("out of order frames and clock rollback rejected until reset", [] {
        std::atomic<int> calls{0}; Audio audio;
        DualBrainEngine brain(audio, [&] { return std::make_unique<Model>(calls); }, [] { return 10.0; });
        auto s = scene(); s.hazards[1].ttiSeconds = 1;
        check(brain.publish(s), "first frame rejected");
        check(!brain.publish(scene(9.9)), "old frame accepted");
        brain.tick(s.telemetry, 10); auto count = audio.spoken.size();
        brain.tick(s.telemetry, 9); check(audio.spoken.size() == count, "rollback emitted audio");
        brain.reset(); check(brain.publish(scene(1)), "reset retained old frame timestamp");
    });
    test("bounded reflex benchmark (host timing, not phone guarantee)", [] {
        auto s = scene(); s.count = maxHazards;
        for (auto& h : s.hazards) h = {1, HazardType::vehicle, Lane::ego, 30, 9, .9, true, false};
        ReflexEngine reflex; volatile int accumulator = 0;
        const auto start = SteadyClock::now();
        for (int i = 0; i < 100000; ++i) accumulator += int(reflex.evaluate(s, 10).command);
        const auto micros = std::chrono::duration<double, std::micro>(SteadyClock::now() - start).count() / 100000;
        std::cout << "  64 hazards: mean " << micros << " us per reflex evaluation (" << accumulator << ")\n";
        check(micros < 5000, "host reflex average exceeds 5 ms");
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
