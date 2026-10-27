#include "learndrive/coaching/TemporalSupport.hpp"
#include <algorithm>
#include <cmath>

namespace ld::coaching {
namespace {
bool recent(double now,double time,double age) noexcept {
    return std::isfinite(now)&&std::isfinite(time)&&now>=0&&time>=0&&time<=now&&now-time<age;
}
bool validBox(const TrackingObservation& o) noexcept {
    return std::isfinite(o.x)&&std::isfinite(o.y)&&std::isfinite(o.w)&&std::isfinite(o.h)&&
        o.x>=0&&o.y>=0&&o.w>0&&o.h>0&&o.x+o.w<=1.0001f&&o.y+o.h<=1.0001f;
}
}
void ObservationSynchronizer::beginSource(uint64_t source) noexcept {source_=source;clocks_={};}
ObservationResult ObservationSynchronizer::accept(ObservationChannel channel,uint64_t source,uint64_t sequence,
    double capture,double now,double maxAge) noexcept {
    const auto index=static_cast<int>(channel);
    if(!source||source!=source_||index<0||index>=3||!std::isfinite(maxAge)||maxAge<=0||
       !std::isfinite(capture)||!std::isfinite(now)||capture<0||now<0||capture>now)return ObservationResult::Invalid;
    auto& clock=clocks_[size_t(index)];
    if(clock.seen) {
        if(capture==clock.capture&&(!sequence||!clock.sequence||sequence==clock.sequence))return ObservationResult::Duplicate;
        if(capture<=clock.capture||(sequence&&clock.sequence&&sequence<=clock.sequence))return ObservationResult::OutOfOrder;
    }
    if(now-capture>=maxAge)return ObservationResult::Stale;
    clock={capture,sequence,true};return ObservationResult::Accepted;
}
void ObjectTracker::beginSource(uint64_t source) noexcept {source_=source;tracks_={};}
bool ObjectTracker::current(const TrackingObservation& observation,double now) noexcept {
    return recent(now,observation.time,maxVisionAgeSeconds);
}
ObservationResult ObjectTracker::observe(const TrackingObservation& observation,double now) noexcept {
    if(!source_||observation.key.source!=source_||!observation.key.id||observation.key.space<=0||
       !validBox(observation)||!std::isfinite(observation.confidence)||observation.confidence<0||observation.confidence>1||
       !std::isfinite(observation.time)||!std::isfinite(now)||observation.time<0||observation.time>now)return ObservationResult::Invalid;
    if(!current(observation,now))return ObservationResult::Stale;
    TrackHistory* slot=nullptr;
    for(auto& track:tracks_)if(track.used&&track.latest.key==observation.key){slot=&track;break;}
    if(slot) {
        if(observation.time==slot->latest.time)return ObservationResult::Duplicate;
        if(observation.time<slot->latest.time)return ObservationResult::OutOfOrder;
        if(observation.category>=0&&slot->latest.category>=0&&observation.category!=slot->latest.category)return ObservationResult::Invalid;
        const double gap=observation.time-slot->latest.time;
        if(gap>=10) {*slot={};slot->used=true;slot->firstSeen=observation.time;}
        const double dx=observation.x+observation.w*.5-slot->latest.x-slot->latest.w*.5;
        const double dy=observation.y+observation.h*.5-slot->latest.y-slot->latest.h*.5;
        // A continuity diagnostic, not an estimate of lane, speed or future path.
        slot->continuous=gap<=.75&&std::hypot(dx,dy)<=.35;
    } else {
        for(auto& track:tracks_)if(!track.used||!recent(now,track.latest.time,10)){slot=&track;break;}
        if(!slot)slot=&*std::min_element(tracks_.begin(),tracks_.end(),[](const auto& a,const auto& b){return a.latest.time<b.latest.time;});
        *slot={};slot->used=true;slot->firstSeen=observation.time;
    }
    slot->latest=observation;++slot->observations;
    while(slot->count) {
        const auto oldest=(slot->cursor+slot->history.size()-slot->count)%slot->history.size();
        if(observation.time-slot->history[oldest].time<=10)break;
        --slot->count;
    }
    const auto previous=slot->count?slot->history[(slot->cursor+slot->history.size()-1)%slot->history.size()].time:-infinity;
    if(observation.time-previous>=.249999) {
        slot->history[slot->cursor]=observation;slot->cursor=(slot->cursor+1)%slot->history.size();
        slot->count=std::min(slot->count+1,slot->history.size());
    }
    return ObservationResult::Accepted;
}
const TrackHistory* ObjectTracker::find(const TrackingKey& key,double now) const noexcept {
    if(key.source!=source_)return nullptr;
    for(const auto& track:tracks_)if(track.used&&track.latest.key==key&&recent(now,track.latest.time,10))return &track;
    return nullptr;
}
void ObjectTracker::expire(double now) noexcept {
    if(!std::isfinite(now)||now<0)return;
    for(auto& track:tracks_)if(track.used&&now>=track.latest.time&&now-track.latest.time>=10)track={};
}
size_t ObjectTracker::count(double now) const noexcept {
    size_t count=0;for(const auto& track:tracks_)if(track.used&&recent(now,track.latest.time,10))++count;return count;
}
void VehicleStateEstimator::beginSource(uint64_t source) noexcept {
    source_=source;samples_={};cursor_=count_=0;latest_={};segmentStart_=lastStored_=-infinity;
}
ObservationResult VehicleStateEstimator::observe(uint64_t source,double capture,double speed,bool valid,double now) noexcept {
    if(!source||source!=source_||!std::isfinite(now)||!std::isfinite(capture)||capture<0||capture>now)return ObservationResult::Invalid;
    if(capture==latest_.time)return ObservationResult::Duplicate;
    if(capture<latest_.time)return ObservationResult::OutOfOrder;
    if(!recent(now,capture,maxVisionAgeSeconds))return ObservationResult::Stale;
    valid=valid&&std::isfinite(speed)&&speed>=0&&speed<=300;
    const double gap=capture-latest_.time;
    const bool implausible=valid&&latest_.valid&&gap>0&&gap<=.5&&std::abs(speed-latest_.speed)/gap>40;
    if(!valid||implausible||!latest_.valid||gap>.5)segmentStart_=capture;
    latest_={capture,valid?speed:0,valid&&!implausible};
    if(capture-lastStored_>=.099999||!latest_.valid) {
        samples_[cursor_]=latest_;cursor_=(cursor_+1)%samples_.size();count_=std::min(count_+1,samples_.size());lastStored_=capture;
    }
    return ObservationResult::Accepted;
}
MotionTrend VehicleStateEstimator::trend(double now) const noexcept {
    if(!latest_.valid||!recent(now,latest_.time,maxVisionAgeSeconds)||latest_.time-segmentStart_<1)return MotionTrend::Unknown;
    const Sample* reference=nullptr;
    for(size_t i=0;i<count_;++i) {
        const auto& sample=samples_[i];const double span=latest_.time-sample.time;
        if(sample.valid&&sample.time>=segmentStart_&&span>=1&&span<=2.5&&(!reference||sample.time<reference->time))reference=&sample;
    }
    if(!reference)return MotionTrend::Unknown;
    const double delta=latest_.speed-reference->speed;
    if(delta<=-2)return MotionTrend::Decreasing;
    if(delta>=2)return MotionTrend::Increasing;
    return MotionTrend::Steady;
}
size_t VehicleStateEstimator::sampleCount(double now) const noexcept {
    size_t result=0;for(size_t i=0;i<count_;++i)if(recent(now,samples_[i].time,10))++result;return result;
}
} // namespace ld::coaching
