#include "learndrive/BumpDetector.hpp"
#include <algorithm>
#include <cmath>
namespace ld {
namespace {
// Floor for a jolt (m/s^2) and how far above the road's own vibration it must rise.
constexpr double floorJolt=2.0,aboveBackground=3.5,backgroundSeconds=4;
// A second pulse belongs to the rear wheels when it comes one wheelbase later.
constexpr double wheelbaseMin=1.9,wheelbaseMax=3.4;
constexpr double minSpeed=8,maxSpeed=45,roughRms=1.5,minPitch=.10,rollShare=.8;
}
const char* bumpReasonText(BumpReason r) noexcept {
    switch(r) {
    case BumpReason::accepted:return "speed bump: front then rear wheels, nose up and down";
    case BumpReason::speed:return "speed unknown or outside 8-45 km/h";
    case BumpReason::handled:return "phone moved by hand";
    case BumpReason::rough:return "rough road: jolts everywhere";
    case BumpReason::oneAxle:return "no rear-wheel jolt one wheelbase later";
    case BumpReason::oneSided:return "car rolled: one side only (pothole or kerb)";
    case BumpReason::weakPitch:return "no nose-up/nose-down pitch";
    }
    return "unknown";
}
void BumpDetector::reset() noexcept {*this=BumpDetector{};}
double BumpDetector::backgroundRms() const noexcept {return std::sqrt(background2_);}
void BumpDetector::endPulse() noexcept {
    if(!inPulse_)return;
    inPulse_=false;quietSince_=-1;
    if(current_.peak<.6*threshold_)return;
    // Oscillation of one wheel pair closer than 0.12 s is the same pulse.
    if(pulseCount_&&current_.time-pulses_[pulseCount_-1].time<.12) {
        if(current_.peak>pulses_[pulseCount_-1].peak)pulses_[pulseCount_-1]=current_;
        return;
    }
    if(pulseCount_<pulses_.size())pulses_[pulseCount_++]=current_;
}
void BumpDetector::sample(double time,double vertical,double pitchRate,double rollRate,double speedKmh,bool handled) noexcept {
    if(!std::isfinite(time)||time<=last_)return;
    const double dt=last_<0?.01:std::min(time-last_,.05);last_=time;
    const double a=std::isfinite(vertical)?std::abs(vertical):0;
    const double pitch=std::isfinite(pitchRate)?std::abs(pitchRate):0,roll=std::isfinite(rollRate)?std::abs(rollRate):0;
    if(!active_) {
        const double threshold=std::max(floorJolt,aboveBackground*backgroundRms());
        if(time>=refractoryUntil_&&a>=threshold) {
            active_=true;threshold_=threshold;startSpeed_=speedKmh;handled_=handled;
            maxPitch_=pitch;maxRoll_=roll;pulseCount_=0;inPulse_=true;current_={time,a};quietSince_=-1;
            // Long enough for the rear wheels at the measured speed (slowest 8 km/h: 1.9 s).
            const double mps=std::isfinite(speedKmh)&&speedKmh>=minSpeed?speedKmh/3.6:0;
            eventEnd_=time+(mps>0?std::clamp(wheelbaseMax/mps+.35,.6,1.9):1.);
            return;
        }
        // The road's own vibration, learned only outside jolts.
        if(time>=refractoryUntil_)background2_+=(a*a-background2_)*std::min(1.,dt/backgroundSeconds);
        return;
    }
    maxPitch_=std::max(maxPitch_,pitch);maxRoll_=std::max(maxRoll_,roll);handled_=handled_||handled;
    if(a>=.5*threshold_) {
        if(!inPulse_){inPulse_=true;current_={time,a};}
        else if(a>current_.peak)current_={time,a};
        quietSince_=-1;
    } else if(inPulse_) {
        if(quietSince_<0)quietSince_=time;
        if(time-quietSince_>=.06)endPulse();
    }
    if(time>=eventEnd_){endPulse();finish(time);}
}
void BumpDetector::finish(double time) noexcept {
    BumpEvent e;
    e.time=pulseCount_?pulses_[0].time:time;e.pulses=int(pulseCount_);
    for(size_t i=0;i<pulseCount_;++i)e.peakVertical=std::max(e.peakVertical,pulses_[i].peak);
    e.peakPitchRate=maxPitch_;e.peakRollRate=maxRoll_;e.speedKmh=startSpeed_;e.backgroundRms=backgroundRms();
    const bool speedOk=std::isfinite(startSpeed_)&&startSpeed_>=minSpeed&&startSpeed_<=maxSpeed;
    if(!speedOk)e.reason=BumpReason::speed;
    else if(handled_)e.reason=BumpReason::handled;
    else if(e.backgroundRms>roughRms)e.reason=BumpReason::rough;
    else {
        const double mps=startSpeed_/3.6,lo=wheelbaseMin/mps,hi=wheelbaseMax/mps;
        double best=0;
        for(size_t i=1;i<pulseCount_;++i) {
            const double gap=pulses_[i].time-pulses_[0].time;
            if(gap>=lo&&gap<=hi&&pulses_[i].peak>=.35*pulses_[0].peak&&pulses_[i].peak>best) {
                best=pulses_[i].peak;e.secondVertical=pulses_[i].peak;e.axleGapS=gap;
            }
        }
        if(best<=0)e.reason=BumpReason::oneAxle;
        // A car rolled by one side dropping into a pothole pitches little: name the roll first.
        else if(maxRoll_>rollShare*std::max(maxPitch_,minPitch))e.reason=BumpReason::oneSided;
        else if(maxPitch_<minPitch)e.reason=BumpReason::weakPitch;
        else e.reason=BumpReason::accepted;
    }
    const size_t slot=(head_+count_)%done_.size();
    done_[slot]=e;
    if(count_<done_.size())++count_; else head_=(head_+1)%done_.size();
    active_=false;inPulse_=false;pulseCount_=0;refractoryUntil_=time+.8;
}
bool BumpDetector::pop(BumpEvent& out) noexcept {
    if(!count_)return false;
    out=done_[head_];head_=(head_+1)%done_.size();--count_;return true;
}
}
