#pragma once
#include <array>
#include <cstddef>
#include <limits>
namespace ld {
// Why a felt jolt was or was not taken as a road-wide speed bump.
enum class BumpReason : int { accepted=0,speed=1,handled=2,rough=3,oneAxle=4,oneSided=5,weakPitch=6 };
const char* bumpReasonText(BumpReason) noexcept;
// One jolt the phone felt, accepted or not (rejected ones are kept for diagnostics and tuning).
struct BumpEvent {
    double time=-std::numeric_limits<double>::infinity(); // the first (front-axle) pulse
    double peakVertical{},secondVertical{},axleGapS{},peakPitchRate{},peakRollRate{},speedKmh{},backgroundRms{};
    int pulses{};
    BumpReason reason=BumpReason::speed;
    bool accepted() const noexcept {return reason==BumpReason::accepted;}
};
// Recognises a car crossing a road-wide speed bump from the phone's motion sensor:
// a vertical jolt well above the road's background vibration, a second jolt one
// wheelbase later (the rear wheels, 1.9-3.4 m at the measured speed), and a nose-up /
// nose-down pitch with little roll (a pothole or a kerb hit by one side rolls the car).
// Rough roads, unknown or unsuitable speed and a hand-held phone are rejected.
class BumpDetector {
public:
    void reset() noexcept;
    // One sample in the level car frame: vertical acceleration (m/s^2, gravity removed),
    // pitch and roll rates (rad/s). speedKmh is NaN when unknown. handled: the phone is
    // being moved by hand.
    void sample(double time,double vertical,double pitchRate,double rollRate,double speedKmh,bool handled) noexcept;
    // Oldest finished jolt not yet taken; false when none.
    bool pop(BumpEvent& out) noexcept;
    double backgroundRms() const noexcept;
private:
    struct Pulse {double time{},peak{};};
    void endPulse() noexcept;
    void finish(double time) noexcept;
    std::array<Pulse,8> pulses_{};size_t pulseCount_{};
    std::array<BumpEvent,8> done_{};size_t head_{},count_{};
    double background2_=.25,last_=-1,eventEnd_=-1,refractoryUntil_=-1,threshold_{};
    double startSpeed_=std::numeric_limits<double>::quiet_NaN(),maxPitch_{},maxRoll_{},quietSince_=-1;
    bool active_{},handled_{},inPulse_{};
    Pulse current_{};
};
}
