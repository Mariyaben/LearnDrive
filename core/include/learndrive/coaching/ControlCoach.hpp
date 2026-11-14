#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;class ShortMemory;class TaskCoach;
// Speed bands (km/h) of a typical 5-speed manual car. up[g]: change up to gear g
// above this speed; down[g]: change down from gear g below this speed. These are
// instructor rules of thumb, configurable per car, not measurements of the gearbox.
struct GearBands {
    std::array<double,6> up{0,0,10,22,37,52};
    std::array<double,6> down{0,0,0,17,30,45};
};
// Instructor-style vehicle-control coaching from measured speed, speed trend,
// slope and route progress. It tracks the gear it last ADVISED for the measured
// speed; it never claims to know the selected gear or pedal positions, and it
// never tells the learner that it is clear to move.
class ControlCoach {
public:
    void enable(bool value) noexcept {enabled_=value;reset();}
    bool enabled() const noexcept {return enabled_;}
    void reset() noexcept;
    void setBands(const GearBands& bands) noexcept {bands_=bands;}
    const GearBands& bands() const noexcept {return bands_;}
    // support: 0 minimal (slope only), 1 balanced, 2 guided (all gear changes, moving off).
    void observe(const WorldState&,const ShortMemory&,const TaskCoach&,int support) noexcept;
    size_t proposals(const WorldState&,Candidate* out,size_t capacity) const noexcept;
    static bool evidence(const Candidate&,const WorldState&,double now) noexcept;
    int advisedGear() const noexcept {return gear_;}
    // The gear advice for a climb or descent at this speed, from the same speed bands (rules of
    // thumb, never the selected gear). The hill's gear is second when steep (>= 10 %), else third.
    // gear 0: nothing to name (this speed's own gear is already at or below it); 2 or 3: change
    // down to it; -1: change down a gear (climbing fast: one gear at a time). brakeFirst: going
    // downhill two gears above it, slow down before changing down.
    struct HillGear {int gear{};bool brakeFirst{};};
    static HillGear hillGear(const GearBands&,double gradePercent,double speedKmh) noexcept;
    // What a bend ahead asks for at this speed, given the speed the bend is comfortable at. Up to
    // 3 km/h over it: nothing. Up to 10 over: ease off. More: brake gently. The gear is the one
    // the bend's speed belongs to (second or third), named only when slowing is needed and the
    // present speed belongs to a higher gear.
    struct BendAdvice {bool ease{},brake{};int gear{};bool any() const noexcept {return ease||brake;}};
    static BendAdvice bendAdvice(const GearBands&,double adviseKmh,double speedKmh) noexcept;
    std::string json() const;
private:
    struct Active {int prompt{};uint64_t id{};double until=-1;};
    void propose(int prompt,double now,double seconds) noexcept;
    bool enabled_{};
    GearBands bands_{};
    int gear_{},candidateGear_{},slopeSign_{};
    double candidateSince_=-1,stoppedSince_=-1,slopeClearSince_=-1,exitAt_=-1,driftLeftSince_=-1,driftRightSince_=-1,lastLaneCue_=-1,lastBendCue_=-1,lastGentleBendCue_=-1;
    // The hill last named from the map and when the car was last on it (or named it): its way and steepness.
    double hillAt_=-1;int hillSign_{};bool hillSteep_{};
    bool stopCued_{},moveOffCued_{},lastBendRight_{},routeChangeCued_{};
    uint64_t lastQuietEvent_{},next_{1},lastBump_{},lastBend_{},lastSlope_{};
    // Heading change through the held junction (rad, from the turn rate) and whether it was measured.
    double turned_{},lastYawAt_=-1;bool turnMeasured_{},reroutedInTurn_{};
    std::array<Active,4> active_{};
    std::array<uint32_t,promptCount+1> counts_{};
};
}
