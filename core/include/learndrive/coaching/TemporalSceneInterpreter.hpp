#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ld::coaching {
struct WorldState;
class ShortMemory;
class EventLedger;
enum class EncounterPhase : int { Observed, Continuing, PotentiallyRelevant, Changing, TemporarilyLost, Resolved, Expired };
enum class ResponseEvidence : int { Uncertain, Observed, NotObserved };
enum class SilenceReason : int { None, AlreadyInformed, ResponseObserved, LowValue, UncertainEvidence, ManeuverWorkload, Resolved, Unchanged };
enum class InstructionMeaning : int { None, RoadUserSpace, FollowingGap, TurnPreparation, BendPreparation, GradePreparation, VehicleShift, Critical };
struct EncounterEpisode {
    uint64_t id{},scope{};
    int space{},category=-1;
    double firstSeen=-1,lastSeen=-1,captureTime=-1,changedAt=-1,confidence{},tti=0;
    float x{},y{},w{},h{},dx{},dy{};
    uint32_t observations{},missingFrames{};
    EncounterPhase phase=EncounterPhase::Observed;
    bool used{},fresh{},relevant{},wasRelevant{},closing{},inPath{},leadSupported{},coached{};
};
struct InterpretedScene {
    uint64_t source{},revision{},generation{},routeId{},routeEvent{},routeEpoch{};
    double now{},routeDistance=0,routeEta=0,lastInstructionAt=-1;
    int routeFeature{},routeWindow{},speedTrend{},lastInstruction{};
    bool routeFresh{},routeProvider{},speedValid{},cameraFresh{},lastInstructionDelivered{};
    bool lastInstructionProvider{};
    ResponseEvidence response=ResponseEvidence::Uncertain;
    SilenceReason silence=SilenceReason::None;
    std::array<EncounterEpisode,64> encounters{};
    size_t count{};
};

// Scalar interpretation around existing identities, not a second image tracker.
// Missing evidence is explicitly lost/expired, never a fabricated path or clear road.
class TemporalSceneInterpreter {
public:
    void reset(uint64_t source=0) noexcept;
    const InterpretedScene& observe(const WorldState&,const ShortMemory&,const EventLedger&,uint64_t revision) noexcept;
    const InterpretedScene& scene() const noexcept {return scene_;}
    std::string encode(const WorldState&,const EventLedger&) const;
private:
    InterpretedScene scene_{};
    double lastCamera_=-1,lastNow_=-1;
    uint64_t lastCandidates_{};
};
InstructionMeaning instructionMeaning(int prompt) noexcept;
const char* encounterName(EncounterPhase) noexcept;
const char* responseName(ResponseEvidence) noexcept;
const char* silenceName(SilenceReason) noexcept;
} // namespace ld::coaching
