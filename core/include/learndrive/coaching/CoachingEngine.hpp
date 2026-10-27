#pragma once
#include "learndrive/types.hpp"
#include "learndrive/coaching/TemporalSupport.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace ld::coaching {
enum class Mode : int { SILENCE=0, COACH=1, WARN=2, DEFER=3 };
enum class Profile : int { Minimal=0, Balanced=1, Guided=2 };
enum class IdentitySpace : int { None=0, Attention=1, Optical=2, Spatial=3, Route=4, Slope=5, Bend=6, Engine=7, Tactical=8 };
enum class EventKind : int { None=0, Attention=1, Approach=2, Critical=3, Route=4, Slope=5, Bend=6, Shift=7, Status=8 };
enum class EventState : int { Candidate=0, Active=1, Deferred=2, Accepted=3, Resolved=4, Expired=5 };
enum class SpeedTrend : int { Unknown=0, Steady=1, Increasing=2, Decreasing=3 };
enum class Block : int { None=0, Inactive=1, Stale=2, Unsupported=3, Cooldown=4, Duplicate=5, Audio=6, Revision=7, CriticalDisabled=8, DriverResponse=9 };
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
};
struct WorldState {
    uint64_t sourceId{},frameSequence{};
    double now{};
    Telemetry telemetry{};
    bool active{},audioReady{},allowCritical{},simulation{};
    double roadCaptureTime=-infinity,engineCaptureTime=-infinity;
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
    SpeedTrend speedTrend(double now) const noexcept;
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
struct PolicyContext {const WorldState& world;const ShortMemory& memory;};
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
class SafetyValidator {
public:
    Block validate(const Candidate&,const WorldState&,double now) const noexcept;
    static Prompt neutral(Prompt,bool mechanicalMeasured) noexcept;
};
struct Ticket {
    Candidate candidate{};
    uint64_t sequence{},sourceId{},revision{},eventNumber{};
    double decidedAt{},acceptedAt=-infinity;
    bool valid{},accepted{},voiceSubmitted{},onset{};
};
struct Decision {
    Mode mode=Mode::SILENCE;
    Block blocked=Block::None;
    Ticket ticket{};
    bool eligible{};
};
struct DeliveryStats {uint64_t logical{},voiceSubmitted{},playbackOnsets{},onsetRejected{},selectionRejected{},duplicates{},deferred{},expired{};};
class CoachingEngine {
public:
    bool configure(int profile,bool voice,uint64_t revision) noexcept;
    void beginSource(uint64_t source) noexcept;
    void invalidateEvidence() noexcept {pending_={};accepted_={};deferred_={};decision_={};memory_.clear();}
    bool configured() const noexcept {return configured_;}
    bool voice() const noexcept {return voice_;}
    uint64_t revision() const noexcept {return revision_;}
    double interval() const noexcept;
    Decision decide(const WorldState&,uint64_t sequence) noexcept;
    bool pending(uint64_t sequence,int prompt,const WorldState&,double now) const noexcept;
    bool accept(uint64_t sequence,int prompt,const WorldState&,double now,bool voiceSubmitted) noexcept;
    bool playbackValid(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) const noexcept;
    bool playbackStarted(uint64_t sequence,int prompt,const WorldState&,double now,uint64_t revision) noexcept;
    void setPolicy(const DecisionPolicy* policy) noexcept {policy_=policy?policy:&balanced_;pending_={};accepted_={};}
    const Decision& lastDecision() const noexcept {return decision_;}
    const Ticket& acceptedTicket() const noexcept {return accepted_;}
    const ShortMemory& memory() const noexcept {return memory_;}
    const EventLedger& ledger() const noexcept {return ledger_;}
    const ObjectTracker& tracks() const noexcept {return tracks_;}
    const DeliveryStats& stats() const noexcept {return stats_;}
    size_t diagnostics(double now,double* out,size_t capacity) const noexcept;
    std::string diagnosticsJson(double now) const;
    const char* reason() const noexcept;
private:
    bool ticketValid(const Ticket&,uint64_t,int,const WorldState&,double) const noexcept;
    BalancedPolicy balanced_;
    const DecisionPolicy* policy_=&balanced_;
    SafetyValidator validator_;
    ShortMemory memory_;
    EventLedger ledger_;
    ObservationSynchronizer synchronizer_;
    ObjectTracker tracks_;
    Profile profile_=Profile::Balanced;
    bool configured_{},voice_=true;
    uint64_t revision_{},source_{};
    double lastNow_=-infinity,lastOrdinary_=-infinity,lastWarning_=-infinity;
    Ticket pending_{},accepted_{},deferred_{};
    Decision decision_{};
    DeliveryStats stats_{};
    const char* policyReason_=nullptr;
};
} // namespace ld::coaching
