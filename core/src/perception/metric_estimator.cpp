#include "learndrive/perception/metric_estimator.hpp"

namespace ld::perception {
namespace {
double widthFor(int category) noexcept {
    switch (category) {
    case 2: return 1.8; // car
    case 5: case 7: return 2.5; // bus/truck
    case 1: case 3: return .8; // bicycle/motorcycle
    default: return infinity; // pedestrians vary too much for width ranging
    }
}
}
void MetricEstimator::reset() noexcept { tracks_ = {}; }
double KalmanFilter1D::update(double measured,double dt) noexcept {
    if(!initialized_){reset(measured);return measured;}
    variance_+=.8*dt;
    const double noise=std::max(.04,measured*measured*.01),gain=variance_/(variance_+noise);
    value_+=gain*(measured-value_);variance_*=(1-gain);return value_;
}
MetricEstimate MetricEstimator::update(uint64_t id, int category, double pixels,
                                       double fy, double time) noexcept {
    MetricEstimate out;
    const double width=widthFor(category);
    if (!id || !std::isfinite(width) || !std::isfinite(pixels) || pixels < 2 ||
        !std::isfinite(fy) || fy <= 0 || !std::isfinite(time) || time < 0) return out;
    const double measured=fy*width/pixels;
    if (!std::isfinite(measured) || measured <= 0 || measured > 250) return out;
    Track* track=nullptr;
    for (auto& t:tracks_) if (t.used && t.id==id) { track=&t; break; }
    if (!track) {
        for (auto& t:tracks_) if (!t.used) { track=&t; break; }
        if (!track) track=&tracks_[id%tracks_.size()];
        *track={};track->id=id;track->used=true;
    }
    const double dt=time-track->time;
    if(dt<=0)return out;
    if(dt>.5){track->samples=track->cursor=0;track->filter.reset(measured);}
    out.distanceM=track->filter.update(measured,std::isfinite(dt)?dt:0);
    track->distances[track->cursor]=out.distanceM;track->times[track->cursor]=time;
    track->cursor=(track->cursor+1)%5;track->samples=std::min(5u,track->samples+1);track->time=time;
    // Least-squares slope over exactly the latest five filtered observations.
    if(track->samples==5) {
        double meanT=0,meanZ=0,numerator=0,denominator=0;
        for(size_t i=0;i<5;++i){meanT+=track->times[i]/5;meanZ+=track->distances[i]/5;}
        for(size_t i=0;i<5;++i){const double delta=track->times[i]-meanT;
            numerator+=delta*(track->distances[i]-meanZ);denominator+=delta*delta;}
        if(denominator>1e-8)out.relativeVelocityMps=numerator/denominator;
        if(out.relativeVelocityMps<-.1)out.ttiSeconds=out.distanceM/-out.relativeVelocityMps;
    }
    out.valid=true;
    return out;
}
} // namespace ld::perception
