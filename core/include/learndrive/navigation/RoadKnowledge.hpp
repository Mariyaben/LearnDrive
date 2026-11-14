#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
namespace ld::navigation {
// A point on a path ahead of the car: position and distance along the path (m).
struct GeoPoint {double lat{},lon{},s{};};

// Speed bumps the coach knows about: mapped in OpenStreetMap (trusted as mapped) or
// learned on this phone from the motion sensor. A learned bump is used for coaching only
// once it has been felt on two separate passes, and stops being used when it has been
// driven over (while the sensor was working) as often as it was felt without being felt.
struct Bump {
    uint64_t id{};
    double lat{},lon{},heading{};
    bool mapped{};
    int observations{},misses{};
    double lastFelt=-std::numeric_limits<double>::infinity(),passedAt=-std::numeric_limits<double>::infinity();
    double lastAlong=std::numeric_limits<double>::quiet_NaN(),lastAlongTime=-std::numeric_limits<double>::infinity();
    bool usable() const noexcept {return mapped||(observations>=2&&misses<observations);}
};
class BumpMap {
public:
    static constexpr size_t capacity=4000;
    static constexpr uint64_t learnedFlag=uint64_t(1)<<61;
    void clear() noexcept {bumps_.clear();}
    // An OpenStreetMap traffic-calming node.
    bool addMapped(uint64_t nodeId,double lat,double lon);
    // A learned bump saved from an earlier drive on this phone.
    bool restore(double lat,double lon,double heading,int observations,int misses);
    // The motion sensor felt a road-wide bump here while travelling `heading` (degrees).
    // Returns the bump's id (an existing bump within 15 m, or a new learned one), 0 if not stored.
    uint64_t felt(double time,double lat,double lon,double heading);
    // Each GPS fix: a learned bump driven over (within 8 m, along its road) without being
    // felt while the sensor was working counts as a miss.
    void travel(double time,double lat,double lon,double heading,bool headingValid,double speedKmh,double accuracyM,bool sensing) noexcept;
    size_t size() const noexcept {return bumps_.size();}
    const Bump& at(size_t i) const {return bumps_[i];}
    size_t learnedCount() const noexcept;
    size_t usableLearnedCount() const noexcept;
    struct Hit {double distanceM=std::numeric_limits<double>::infinity();uint64_t id{};bool learned{};};
    // First usable bump within 8 m of a path ahead (points with distance along it), up to maxS.
    Hit along(const GeoPoint* path,size_t count,double maxS) const noexcept;
private:
    std::vector<Bump> bumps_;
    uint64_t nextLearned_=1;
};

// What the provider's route (e.g. the Google route while navigating) shows ahead.
struct RouteAhead {
    bool matched{},bendsKnown{};
    double fixTime=-std::numeric_limits<double>::infinity(),progressM{};
    double bendDistanceM=std::numeric_limits<double>::infinity(),bendAngle{};
    uint64_t bendId{};bool bendRight{};
    // Sharp (blind: the horn), and the speed its tightest stretch is comfortable at (km/h).
    bool bendSharp{};double bendAdviseKmh=std::numeric_limits<double>::infinity();
    double bumpDistanceM=std::numeric_limits<double>::infinity();
    uint64_t bumpId{};bool bumpLearned{};
    // The road's grade here from the provider's elevation along this route (percent, + uphill,
    // smoothed over about 100 m); only where the elevation profile covers the car.
    bool gradeKnown{};double gradePercent{};
    // The next sustained climb or descent (>= 6 % over >= 100 m) within 600 m, or the one the car is
    // on: distance to its start (negative: that far onto it already), mean grade (signed %),
    // steepest grade, and the length still ahead.
    double slopeDistanceM=std::numeric_limits<double>::infinity(),slopeGradePercent{},slopeSteepestPercent{},slopeLengthM{};
    uint64_t slopeId{};
};
// The provider's route line, held in memory only for this route (never stored or exported).
// Sharp bends (>= 55 degrees of turning within 35 m, the same rule as the junction scout)
// are found on the route the car will actually take. A bend at one of the provider's own
// maneuvers is that turn, not a bend; bends are reported only while the maneuver positions
// are fresh, so a turn is never announced as a bend.
// Since 0.8.6 also the bends that are not sharp but still want the right speed (35 degrees or
// more within 40 m), and for every bend the speed its tightest 20 m is comfortable at.
class RouteLine {
public:
    static constexpr uint64_t bendFlag=uint64_t(1)<<62;
    // lat,lon pairs. count 0 clears; false (and cleared) when the line is unusable.
    bool load(const double* latLon,size_t count);
    void clear() noexcept;
    // Distances (m) from the car to each remaining maneuver, nearest first, reported at `time`.
    void maneuvers(double time,const double* aheadM,size_t count) noexcept;
    bool fix(double time,double lat,double lon,double accuracyM,double bearingDeg,bool bearingValid,double speedKmh) noexcept;
    RouteAhead ahead(double now,const BumpMap& bumps) const noexcept;
    bool loaded() const noexcept {return !points_.empty();}
    size_t bendCount() const noexcept {return bends_.size();}
    static constexpr uint64_t slopeFlag=uint64_t(1)<<60;
    // Elevation along this line: lat, lon, metres triples (e.g. Google's Elevation API sampled along
    // the route), in memory only with the line. Samples more than 25 m off the line are ignored.
    // Replaces any earlier profile; false (and none kept) when it covers under 100 m of the line.
    bool elevation(const double* latLonElev,size_t count);
    bool elevationKnown() const noexcept {return !grade_.empty();}
    size_t slopeCount() const noexcept {return slopes_.size();}
    // Where the profile covers the line (m along it); 0,0 without one.
    double elevationStart() const noexcept {return grade_.empty()?0:gradeStart_;}
    double elevationEnd() const noexcept {return grade_.empty()?0:gradeStart_+double(grade_.size()-1)*10;}
    // Where the road will be `seconds` of driving ahead (at least 15 m, at most 120 m along the
    // line): its direction from the car, radians, + left of the line's heading at the car. NaN
    // unless matched within the last 2.5 s. Aims the detector's zoomed views into bends.
    double aim(double now,double seconds) const noexcept;
    // The car's distance along the line at its last matched fix (m).
    double progress() const noexcept {return progress_;}
    bool matched() const noexcept {return matched_;}
private:
    struct Point {double x{},y{},s{};};
    struct Bend {double s{},angle{};uint64_t id{};bool sharp=true;double adviseKmh=20;};
    Point at(double s) const noexcept;
    double heading(double s) const noexcept;
    double gradeAt(double s) const noexcept;
    struct Slope {double start{},end{},grade{},steepest{};uint64_t id{};};
    std::vector<double> grade_;double gradeStart_{};
    std::vector<Slope> slopes_;uint64_t nextSlope_{};
    std::vector<Point> points_;
    std::vector<Bend> bends_;
    std::array<double,24> turns_{};size_t turnCount_{};bool turnsTruncated_{};
    double turnsTime_=-std::numeric_limits<double>::infinity();
    double lat0_{},lon0_{},kx_{1},ky_{110540};
    uint64_t generation_{};
    double progress_{},fixTime_=-std::numeric_limits<double>::infinity(),speedMps_{};
    bool matched_{};int consistent_{};
};
}
