#include "learndrive/perception/camera_calibration.hpp"
#include "learndrive/perception/metric_estimator.hpp"
#include <cmath>
#include <iostream>
using namespace ld;
using namespace ld::perception;
int main() {
    const auto level=calibrateHorizon({0,gravity,0},500,500,320,240);
    if (!level.valid || std::abs(level.pitchRadians)>1e-9 || std::abs(level.rollRadians)>1e-9) return 1;
    MetricEstimator estimator;
    auto first=estimator.update(7,2,90,500,1.0); // 10 m car
    MetricEstimate second;
    for(int i=1;i<=4;++i)second=estimator.update(7,2,90+10*i,500,1.0+.1*i);
    if (!first.valid || !second.valid || !(second.relativeVelocityMps<0) || !std::isfinite(second.ttiSeconds)) return 2;
    if (estimator.update(8,0,100,500,1.0).valid) return 3; // no unsafe pedestrian width prior
    std::cout << "horizon homography and metric estimator passed\n";
}
