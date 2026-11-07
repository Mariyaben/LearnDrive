#pragma once
#include "learndrive/types.hpp"
#include "learndrive/coaching/TemporalSupport.hpp"
#include "learndrive/coaching/TemporalSceneInterpreter.hpp"
#include "learndrive/coaching/TaskCoach.hpp"
#include "learndrive/coaching/DriveCoach.hpp"
#include "learndrive/coaching/ControlCoach.hpp"
#include "learndrive/coaching/RiskCoach.hpp"
#include "learndrive/coaching/SituationPlanner.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace ld::coaching {
enum class Mode : int { SILENCE=0, COACH=1, WARN=2, DEFER=3 };
enum class Profile : int { Minimal=0, Balanced=1, Guided=2 };
enum class IdentitySpace : int { None=0, Attention=1, Optical=2, Spatial=3, Route=4, Slope=5, Bend=6, Engine=7, Tactical=8, Control=9 };
enum class EventKind : int { None=0, Attention=1, Approach=2, Critical=3, Route=4, Slope=5, Bend=6, Shift=7, Status=8 };
enum class EventState : int { Candidate=0, Active=1, Deferred=2, Accepted=3, Resolved=4, Expired=5 };
enum class SpeedTrend : int { Unknown=0, Steady=1, Increasing=2, Decreasing=3 };
enum class Block : int { None=0, Inactive=1, Stale=2, Unsupported=3, Cooldown=4, Duplicate=5, Audio=6, Revision=7, CriticalDisabled=8, DriverResponse=9 };
enum class DeliveryStage : int { Selected=0,Scheduled=1,Submitted=2,Started=3,Interrupted=4,Expired=5,Failed=6,VisualOnly=7,Completed=8 };
struct Identity {
    IdentitySpace space=IdentitySpace::None;
    uint64_t id{},scope{};
    bool operator==(const Identity& b) const noexcept {return space==b.space&&id==b.id&&scope==b.scope;}
};
struct ActorObservation {
    Identity identity{};
    double captureTime=-infinity,confidence{},tti=infinity;
    int category=-1;
    bool relevant{},inPath{},opticalClosing{},leadSupported{};
    float x{},y{},w{},h{}; // Capture-frame normalized geometry, never metric depth.
};
struct RouteContext {
    uint64_t routeId{},eventId{},epoch{};
    double captureTime=-infinity,distanceM=infinity,etaSeconds=infinity,gpsTime=-infinity;
    int feature{};
    bool valid{},provider{};
    // Provider (Google) step detail: maneuver (TurnKind 1-9), side for keep/fork (-1 left,
    // +1 right), roundabout exit, and the step after this one (kind, metres after it).
    int maneuver{},side{},roundaboutExit{},nextManeuver{};
    double nextGapM=infinity;
};
// Candidates are bounded outputs of existing measured-scene producers. Their
// evidence clock never becomes the rules tick clock. No generated text enters.
struct Candidate {
    Prompt prompt=Prompt::none;
    int origin{};
    Identity identity{};
    EventKind kind=EventKind::None;
    double captureTime=-infinity,maxAge=maxVisionAgeSeconds,expiresAt=infinity;
    double confidence{},tti=infinity;
    bool supported{},urgent{},critical{},mechanicalMeasured{};
    int actorCategory=-1,evidenceKind{};
    float x{},y{},w{},h{};
    int lessonStep{}; // Native task action, zero for the existing candidate producers.
    // Composition: the spoken utterance id (0 = this prompt's own recording) and
    // other instructions it also delivers (recorded in the ledger on acceptance).
    int utterance{};
    std::array<Identity,3> also{};
    std::array<Prompt,3> alsoPrompt{};
    std::array<EventKind,3> alsoKind{};
    std::array<int,3> alsoStep{}; // A junction lesson step said in the same sentence.
    std::array<uint64_t,3> alsoEpoch{};
    size_t alsoCount{};
    uint64_t lessonEpoch{};
};
struct WorldState {
    uint64_t sourceId{},frameSequence{};
    double now{};
    Telemetry telemetry{};
    bool active{},audioReady{},allowCritical{},simulation{};
    double roadCaptureTime=-infinity,engineCaptureTime=-infinity;
    // Lane lines near the car (normalized image x), from the lane model on marked roads.
    double laneTime=-infinity;
    float laneLeft=-1,laneRight=-1;
    bool laneValid{};
    // Road knowledge ahead from the map (no destination needed): bump and sharp bend.
    double mapTime=-infinity,bumpAheadM=infinity,bendAheadM=infinity;
    uint64_t bumpId{},bendId{};
    bool bendRight{};
    // Indian road practice: horn at blind bends, junction entry and when passing.
    bool hornRules=true;
    // Navigation says the learner is pulling up at the destination: a stop here is the
    // end of the drive, not a pause before moving off again.
    bool arriving{};
    // Navigation is finding a new route (a missed turn or a better way).
    bool rerouting{};
    // Horizon row (fraction of image height) from the measured camera pitch. A camera
    // pointing up puts the horizon low in the image; image geometry is relative to it.
    double horizon=.5;
    bool horizonMeasured{};
    // Top of the image the detector saw (fraction of height): above it (sky, cropped by the
    // detector mosaic) a box is cut off, so its apparent height says nothing about distance.
    double visibleTop{};
    int roadBend{},gradeBand{};
    bool roadValid{},engineValid{};
    // Independent vehicle input. A candidate's mechanicalMeasured flag cannot
    // substitute for actual gear/RPM and configured band evidence.
    double engineRpm{},engineLowerRpm{},engineUpperRpm{};
    int engineGear{},engineMaxGear{};
    bool engineUnderLoad{};
    RouteContext route{};
    std::array<ActorObservation,64> actors{};
    size_t actorCount{};
    std::array<Candidate,8> candidates{};
    size_t candidateCount{};
    double captureTime=-infinity;
};
struct MemorySample {
    double time=-infinity,speedKmh{},pitchDegrees{},closestTti=infinity;
    uint64_t frameSequence{},routeEvent{};
    unsigned actors{};
    bool speedValid{},pitchValid{},routeValid{};
    double telemetryTime=-infinity,accelerationMps2{},roadCaptureTime=-infinity,routeCaptureTime=-infinity,routeDistanceM=infinity;
    int roadBend{},routeFeature{};
    bool accelerationValid{},roadValid{};
    std::array<ActorObservation,12> observations{};
};
class ShortMemory {
public:
    void clear() noexcept;
    bool observe(const WorldState&) noexcept;
    size_t size(double now) const noexcept;
    SpeedTrend speedTrend(double now,double since=-infinity) const noexcept;
    const std::array<MemorySample,101>& samples() const noexcept {return samples_;}
private:
    std::array<MemorySample,101> samples_{};
    size_t cursor_{},count_{};
    double last_=-infinity;
    uint64_t source_{};
    VehicleStateEstimator vehicle_;
};
struct Event {
    uint64_t number{},sourceId{};
    Identity identity{};
    EventKind kind=EventKind::None;
    EventState state=EventState::Candidate;
    double firstSeen{},lastSeen{},acceptedAt=-infinity;
    int acceptedSeverity{};
    bool used{};
    Prompt lastPrompt=Prompt::none,acceptedPrompt=Prompt::none;
    double lastConfidence{},lastCapture=-infinity;
    uint64_t acceptances{};
    double acceptedTti=infinity;
    double startedAt=-infinity;
    bool voiceSubmitted{},visualOnly{};
    DeliveryStage deliveryStage=DeliveryStage::Selected;
    bool retryable{};
    bool providerContent{};
    int lessonStep{};
    uint64_t lessonEpoch{};
};
class EventLedger {
public:
    Event& observe(uint64_t source,const Candidate&,double now) noexcept;
    void reconcile(uint64_t source,const WorldState&) noexcept;
    void sourceChanged() noexcept;
    size_t activeCount() const noexcept;
    size_t resolvedCount() const noexcept;
    const std::array<Event,64>& entries() const noexcept {return events_;}
private:
    std::array<Event,64> events_{};
    uint64_t next_=1;
};
struct PolicyContext {const WorldState& world;const ShortMemory& memory;const InterpretedScene* scene=nullptr;const EventLedger* ledger=nullptr;};
struct PolicyChoice {Mode mode=Mode::SILENCE;size_t candidate{};const char* reason=nullptr;};
class DecisionPolicy {
public:
    virtual ~DecisionPolicy()=default;
    virtual PolicyChoice choose(const PolicyContext&) const noexcept=0;
};
class BalancedPolicy final : public DecisionPolicy {
public:
    PolicyChoice choose(const PolicyContext&) const noexcept override;
};
class TemporalPolicy final : public DecisionPolicy {
public:
    PolicyChoice choose(const PolicyContext&) const noexcept override;
private:
    BalancedPolicy fallback_;
};
class SafetyValidator {
public:
    // started: speech already playing; lesson onset windows are not re-applied.
    Block validate(const Candidate&,const WorldState&,double now,bool started=false) const noexcept;
    static Prompt neutral(Prompt,bool mechanicalMeasured) noexcept;
};
struct Ticket {
    Candidate candidate{};
    uint64_t sequence{},sourceId{},revision{},eventNumber{};
    double decidedAt{},acceptedAt=-infinity;
    bool valid{},accepted{},voiceSubmitted{},onset{};
    DeliveryStage stage=DeliveryStage::Selected;
    double priorOrdinary=-infinity,priorWarning=-infinity;
};
struct Decision {
    Mode mode=Mode::SILENCE;
    Block blocked=Block::None;
    Ticket ticket{};
    bool eligible{};
};
struct DeliveryStats {uint64_t logical{},voiceSubmitted{},playbackOnsets{},onsetRejected{},selectionRejected{},duplicates{},deferred{},expired{},submissionFailed{},reservedForTask{};};
class CoachingEngine {
public:
    bool configure(int profile,bool voice,uint64_t revision) noexcept;
    void beginSource(uint64_t source) noexcept;
    void invalidateEvidence() noexcept;
    bool configured() const noexcept {return configured_;}
    bool voice() const noexcept {return voice_;}
    uint64_t revision() const noexcept {return revision_;}
    double interval() const noexcept;
    Decision decide(const WorldState&,uint64_t sequence) noexcept;
    bool pending(uint64_t sequence,int prompt,const WorldState&,double now) const noexcept;
    bool accept(uint64_t sequence,int prompt,const WorldState&,double now,bool voiceSubmitted) noexcept;
    bool playbackValid(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) const noexcept;
    bool playbackStarted(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) noexcept;
    bool continuationValid(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) const noexcept;
    // Diagnostic: why started speech may not continue ("OK" when it may).
    const char* continuationReason(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) const noexcept;
    bool deliveryEvent(uint64_t sequence,int prompt,DeliveryStage,double now) noexcept;
    void setPolicy(const DecisionPolicy* policy) noexcept {policy_=policy?policy:&temporal_;pending_={};accepted_={};}
    const Decision& lastDecision() const noexcept {return decision_;}
    const Ticket& acceptedTicket() const noexcept {return accepted_;}
    const ShortMemory& memory() const noexcept {return memory_;}
    const EventLedger& ledger() const noexcept {return ledger_;}
    const ObjectTracker& tracks() const noexcept {return tracks_;}
    const DeliveryStats& stats() const noexcept {return stats_;}
    const InterpretedScene& interpretedScene() const noexcept {return interpreter_.scene();}
    std::string interpretedSceneJson() const {return interpreter_.encode(lastWorld_,ledger_);}
    size_t diagnostics(double now,double* out,size_t capacity) const noexcept;
    std::string diagnosticsJson(double now) const;
    const char* reason() const noexcept;
    const char* reasonCode() const noexcept;
    void enableJunctionLesson(bool enabled) noexcept {if(task_.enabled()!=enabled){invalidateEvidence();task_.enable(enabled);}}
    const TaskCoach& task() const noexcept {return task_;}
    void enableAutomaticCoaching(bool enabled) noexcept {if(drive_.enabled()!=enabled){enableJunctionLesson(enabled);drive_.enable(enabled);control_.enable(enabled);risk_.enable(enabled);}}
    const DriveCoach& drive() const noexcept {return drive_;}
    // Task-aware scheduling of the automatic coordinator (default on): routine
    // cues yield to a known upcoming lesson window, an episode's follow-up step
    // has its own spacing, and execution quiet survives evidence loss. Off
    // reproduces the 0.7.1 policy for same-interval comparison.
    void enableTaskScheduling(bool enabled) noexcept {if(scheduling_!=enabled){scheduling_=enabled;task_.enableScheduling(enabled);control_.reset();risk_.reset();}}
    const RiskCoach& risk() const noexcept {return risk_;}
    // Composed utterance by id (fragments), or nullptr.
    const Utterance* utterance(int id) const noexcept {for(const auto& u:utterances_)if(u.id&&u.id==id)return &u;return nullptr;}
    // The delivered id of a ticket's candidate: its utterance when composed.
    static int spokenId(const Candidate& c) noexcept {return c.utterance?c.utterance:int(c.prompt);}
    // Instructor-style control coaching (part of task scheduling): actions, not announcements.
    const ControlCoach& control() const noexcept {return control_;}
    void setGearBands(const GearBands& bands) noexcept {control_.setBands(bands);}
    bool taskScheduling() const noexcept {return scheduling_;}
private:
    bool ticketValid(const Ticket&,uint64_t,int,const WorldState&,double) const noexcept;
    BalancedPolicy balanced_;
    TemporalPolicy temporal_;
    const DecisionPolicy* policy_=&temporal_;
    SafetyValidator validator_;
    ShortMemory memory_;
    EventLedger ledger_;
    ObservationSynchronizer synchronizer_;
    ObjectTracker tracks_;
    TemporalSceneInterpreter interpreter_;
    TaskCoach task_;
    DriveCoach drive_;
    ControlCoach control_;
    RiskCoach risk_;
    std::array<Utterance,100> utterances_{};
    size_t utteranceSlot_{};
    Utterance lastSpoken_{};
    double lastSpokenAt_=-infinity;
    WorldState lastWorld_{};
    Profile profile_=Profile::Balanced;
    bool configured_{},voice_=true,scheduling_=true;
    Identity lastOrdinaryLesson_{};
    uint64_t lastOrdinaryLessonEpoch_{};
    int lastOrdinaryOrigin_{};
    uint64_t revision_{},source_{};
    double lastNow_=-infinity,lastOrdinary_=-infinity,lastWarning_=-infinity;
    Ticket pending_{},accepted_{},deferred_{};
    Decision decision_{};
    DeliveryStats stats_{};
    const char* policyReason_=nullptr;
};
} // namespace ld::coaching
