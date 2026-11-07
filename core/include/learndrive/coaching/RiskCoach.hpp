#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;class ShortMemory;class ObjectTracker;class EventLedger;
enum class RiskKind : int { None=0, InPath=1, Crossing=2, Oncoming=3, Roadside=4, Passed=5 };
// One road user's assessed conflict with the learner's path, from its own
// tracked motion. Image geometry only: no metric distance is claimed.
struct ActorRisk {
    uint64_t id{};
    int category=-1,level{};
    RiskKind kind=RiskKind::None;
    double ttc=std::numeric_limits<double>::infinity(),enterSeconds=std::numeric_limits<double>::infinity();
    double expansion{},inwardRate{},rawInward{},foot{},center{},score{};
    // Ground distance (m) from the contact row relative to the horizon; a coarse estimate.
    double distanceM=std::numeric_limits<double>::infinity();
    bool inPath{},left{};
};
// Real-time risk reasoning over all tracked road users. Each tick it assesses
// every fresh optical actor (ego-motion-compensated lateral motion, looming
// time-to-contact, path corridor, oncoming behaviour, vulnerability), ranks
// them, and proposes ONE instruction for the most dangerous. Per-actor memory
// speaks when risk rises, escalates when no response (slowing) is measured,
// and stays quiet once the learner is responding. It never declares the road
// clear and never tells the learner to go.
class RiskCoach {
public:
    void enable(bool value) noexcept {enabled_=value;reset();}
    bool enabled() const noexcept {return enabled_;}
    void reset() noexcept;
    // support: 0 minimal (level>=2), 1 balanced, 2 guided (all levels).
    void observe(const WorldState&,const ShortMemory&,const ObjectTracker&,const EventLedger&,uint64_t source,int support) noexcept;
    size_t proposals(const WorldState&,Candidate* out,size_t capacity) const noexcept;
    static bool evidence(const Candidate&,const WorldState&,double now) noexcept;
    // The learner's own bonnet/dashboard detected as a vehicle (bottom-spanning box).
    static bool egoVehicle(float,float y,float w,float h) noexcept {return y+h>.97f&&w>.42f;}
    const ActorRisk& top() const noexcept {return top_;}
    // Every road user assessed this tick (the planner reasons over all of them).
    size_t riskCount() const noexcept {return count_;}
    const ActorRisk& riskAt(size_t i) const noexcept {return risks_[i<count_?i:0];}
    const ActorRisk* riskForOptical(uint64_t optical) const noexcept;
    uint64_t opticalFor(uint64_t roadUser) const noexcept;
    // Physics: time the learner needs to stop from this speed on this slope.
    static double stoppingSeconds(double speedKmh,double pitchDegrees) noexcept;
    // Last capture time at which the road user once detected as this optical id
    // was seen (any later detector id included); -1 if unknown.
    double lastSeen(uint64_t optical) const noexcept;
    size_t assessed() const noexcept {return count_;}
    std::string json() const;
private:
    struct Episode {uint64_t id{};int spokenLevel{},spokenPrompt{};double spokenAt=-1,lastSeen=-1,crossedOutAt=-1;bool crossing{},passedCue{};RiskKind kind=RiskKind::None;
        std::array<uint64_t,4> optical{};size_t opticalCursor{};};
    // The risk coach's own short-term memory of each road user: detections are
    // associated frame to frame (class, overlap, predicted position) so a road
    // user keeps one identity and motion history when detector IDs change.
    struct Motion {uint64_t id{},optical{};int group=-1;size_t count{},cursor{};double last=-1;std::array<uint64_t,4> opticals{};size_t opticalCursor{};
        std::array<double,10> t{};std::array<float,10> x{},y{},w{},h{};};
    const Motion* associate(uint64_t optical,int category,float x,float y,float w,float h,double capture) noexcept;
    Episode* episode(uint64_t id,double now) noexcept;
    bool enabled_{};
    std::array<ActorRisk,32> risks_{};
    size_t count_{},withHistory_{};
    ActorRisk top_{};
    std::array<Episode,32> episodes_{};
    std::array<Motion,32> motions_{};
    // Learned mask of the learner's own car in the image (mount, ornament,
    // bonnet edge): cells where detections persist while the car is moving.
    // Real road users do not stay in one off-centre/low image cell for long.
    static constexpr int maskW=16,maskH=12;
    std::array<uint16_t,maskW*maskH> maskHits_{};
    uint32_t maskFrames_{};double maskCapture_=-1;
    bool masked(double center,double foot) const noexcept;
    void learnMask(const WorldState&,double v) noexcept;
    uint64_t nextMotion_=1;
    int proposal_{},proposalLevel_{};bool urgent_{},proposalClosing_{};uint64_t proposalId_{};double proposalCapture_=-1,lastGapCue_=-1;
    // Recent turn rate (rad/s, left positive), to remove turning from road users' image motion.
    std::array<double,96> yawT_{},yawV_{};size_t yawCursor_{},yawCount_{};
    double lastOncomingCue_=-1;
    double meanYaw(double from,double to) const noexcept;
    std::array<uint32_t,8> counts_{};
};
}
