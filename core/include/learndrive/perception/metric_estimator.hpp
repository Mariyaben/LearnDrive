#pragma once
#include "learndrive/types.hpp"
#include <array>
#include <cstdint>

namespace ld::perception {
class KalmanFilter1D {
public:
    double update(double measured,double dt) noexcept;
    void reset(double measured) noexcept {value_=measured;variance_=9;initialized_=true;}
private:
    double value_{},variance_=9;bool initialized_{};
};
struct MetricEstimate {
    double distanceM = infinity;
    double relativeVelocityMps{}; // Negative means the target is closing.
    double ttiSeconds = infinity;
    bool valid{};
};

class MetricEstimator {
public:
    MetricEstimate update(uint64_t trackId, int cocoCategory, double pixelWidth,
                          double focalLengthY, double time) noexcept;
    void reset() noexcept;
private:
    struct Track {
        uint64_t id{}; double time=-infinity;unsigned samples{},cursor{};bool used{};
        KalmanFilter1D filter;
        std::array<double,5> distances{},times{};
    };
    std::array<Track, 64> tracks_{};
};
} // namespace ld::perception
