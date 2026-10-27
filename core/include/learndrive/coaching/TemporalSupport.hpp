#pragma once
#include "learndrive/types.hpp"
#include <array>
#include <cstdint>

namespace ld::coaching {
enum class ObservationChannel : int { Camera=0,Telemetry=1,Navigation=2 };
enum class ObservationResult : int { Accepted=0,Duplicate=1,OutOfOrder=2,Stale=3,Invalid=4 };

// Three independent clocks on one monotonic timebase. A duplicate camera frame
// must not stop newer telemetry or route observations from entering context.
class ObservationSynchronizer {
public:
    void beginSource(uint64_t source) noexcept;
    ObservationResult accept(ObservationChannel,uint64_t source,uint64_t sequence,
        double captureTime,double now,double maxAge=maxVisionAgeSeconds) noexcept;
private:
    struct Clock {double capture=-infinity;uint64_t sequence{};bool seen{};};
    uint64_t source_{};
    std::array<Clock,3> clocks_{};
};

struct TrackingKey {
    int space{};
    uint64_t source{},id{};
    bool operator==(const TrackingKey& b) const noexcept {return space==b.space&&source==b.source&&id==b.id;}
};
struct TrackingObservation {
    TrackingKey key{};
    double time=-infinity,confidence{};
    int category=-1;
    float x{},y{},w{},h{};
    bool relevant{};
};
struct TrackHistory {
    TrackingObservation latest{};
    // At most 4 Hz history plus the latest live observation: ten seconds, bounded.
    std::array<TrackingObservation,41> history{};
    double firstSeen=-infinity;
    uint64_t observations{};
    size_t count{},cursor{};
    bool used{},continuous{};
};

// Temporal registry around the existing native image/attention trackers. It
// never merges unlike ID namespaces or invents metric trajectories from boxes.
class ObjectTracker {
public:
    void beginSource(uint64_t source) noexcept;
    ObservationResult observe(const TrackingObservation&,double now) noexcept;
    const TrackHistory* find(const TrackingKey&,double now) const noexcept;
    void expire(double now) noexcept;
    size_t count(double now) const noexcept;
    static bool current(const TrackingObservation&,double now) noexcept;
private:
    uint64_t source_{};
    std::array<TrackHistory,64> tracks_{};
};

enum class MotionTrend : int { Unknown=0,Steady=1,Increasing=2,Decreasing=3 };
class VehicleStateEstimator {
public:
    void beginSource(uint64_t source) noexcept;
    ObservationResult observe(uint64_t source,double captureTime,double speedKmh,
        bool speedValid,double now) noexcept;
    MotionTrend trend(double now) const noexcept;
    size_t sampleCount(double now) const noexcept;
private:
    struct Sample {double time=-infinity,speed{};bool valid{};};
    uint64_t source_{};
    std::array<Sample,101> samples_{};
    size_t cursor_{},count_{};
    Sample latest_{};
    double segmentStart_=-infinity,lastStored_=-infinity;
};
} // namespace ld::coaching
