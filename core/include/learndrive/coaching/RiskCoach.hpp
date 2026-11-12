#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;struct ActorObservation;class ShortMemory;class ObjectTracker;class EventLedger;class LearnerRecord;
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
    // learner: the measured record of this learner's responses ("you first"); null = speak at once.
    void observe(const WorldState&,const ShortMemory&,const ObjectTracker&,const EventLedger&,uint64_t source,int support,LearnerRecord* learner=nullptr) noexcept;
    // The road user whose instruction is being held so the learner can act first: its kind of
    // situation (-1 none), whether the learner is measured slowing, and since when.
    // `at` is when that was last decided, so a display can tell a current hold from an old one.
    struct Hold {int skill=-1;bool responding{};double since=-1,at=-1;};
    Hold hold() const noexcept {return hold_;}
    // Where in the image the car is heading (fraction of the width), learned during the drive; the
    // fallback (the optical centre) until there is enough evidence. A phone turned towards the
    // driver looks a few degrees to one side: people at the road edge then sit in the middle of
    // the image and were taken to be in the path (Nov 8: "Person ahead. Brake now" for people
    // standing at the left edge).
    double headingColumn(double fallback) const noexcept {return heading_.x==heading_.x?heading_.x:fallback;}
    bool headingKnown() const noexcept {return heading_.x==heading_.x;}
    // The proposed instruction is the short form (the situation's name only).
    bool brief() const noexcept {return brief_;}
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
        RiskKind spokenKind=RiskKind::None; // What kind of conflict it was when it was last spoken about.
        int group=-1,spokenGroup=-1;        // Person, two-wheeler or vehicle: now, and when last spoken about.
        std::array<uint64_t,4> optical{};size_t opticalCursor{};
        // "You first": when the coach first noticed this road user mattering soon and when an
        // instruction about it first became due; whether it became a situation for the record (an
        // instruction was said, shortened or left unsaid, or the learner stopped short of it);
        // what the learner's speed did before and after one was spoken; and whether that outcome
        // has been recorded.
        // dueM: how far away an instruction about someone in the path would have come at the speed
        // the learner had when the coach noticed them (slowing early must not hide the situation).
        // before: their speed has fallen since the coach noticed this road user (slowed) and has not
        // come back up by more than 2 km/h from its lowest since (lowKmh).
        // firstSpokenAt: when the first instruction about this road user was accepted.
        int skill=-1;double noticedAt=-1,dueAt=-1,dueM{},lowKmh=std::numeric_limits<double>::infinity(),firstSpokenAt=-1;
        bool situation{},slowed{},before{},after{},measured{},counted{};};
    // The risk coach's own short-term memory of each road user: detections are
    // associated frame to frame (class, overlap, predicted position) so a road
    // user keeps one identity and motion history when detector IDs change.
    struct Motion {uint64_t id{},optical{};int group=-1;size_t count{},cursor{};double last=-1;std::array<uint64_t,4> opticals{};size_t opticalCursor{};
        mutable double headAt=-1,farAt=-1; // When this road user last gave a heading sample.
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
    int proposal_{},proposalLevel_{};bool urgent_{},proposalClosing_{},proposalPathFirst_{};uint64_t proposalId_{};double proposalCapture_=-1,lastGapCue_=-1;
    // Recent turn rate (rad/s, left positive), to remove turning from road users' image motion.
    std::array<double,96> yawT_{},yawV_{};size_t yawCursor_{},yawCount_{};
    double lastOncomingCue_=-1;
    Hold hold_{};bool brief_{};double stopCredit_=-1;
    // The heading column, from two kinds of evidence gathered while driving straight:
    //  - motion: a road user's box drifts sideways, as it grows, in proportion to its distance
    //    from the point the car is heading for (x' = (x - heading) * growth), so each approaching
    //    road user gives heading = x - x'/growth, whatever the road looks like;
    //  - far: road users far ahead stand on the road the car is following.
    // Medians of the last 96 samples of each. Either alone can be off (few approaching road
    // users; far traffic mostly on one side of the road), so the heading is taken only where the
    // two agree; until then, and whenever they part, the optical centre stands.
    struct Heading {std::array<float,96> motion{},far{};size_t motionCount{},motionCursor{},farCount{},farCursor{};
        double x=std::numeric_limits<double>::quiet_NaN(),updatedAt=-1,horizon=std::numeric_limits<double>::quiet_NaN();
        double byMotion=std::numeric_limits<double>::quiet_NaN(),byFar=std::numeric_limits<double>::quiet_NaN();};
    Heading heading_{};
    void sampleHeading(const Motion&,const ActorObservation&,const WorldState&,double fx,double cx) noexcept;
    void updateHeading(double now,double cx) noexcept;
    double meanYaw(double from,double to) const noexcept;
    std::array<uint32_t,8> counts_{};
};
}
