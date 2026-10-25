#pragma once
#include "types.hpp"
#include <cstdint>
#include <vector>

namespace ld::navigation {
struct Coordinate { double latitude{},longitude{}; };
// Only router-supplied maneuver metadata identifies a junction. Polyline bends
// alone cannot establish a junction or the driver's intended turn.
struct Maneuver { size_t shapeIndex{}; int type{}; };
enum class Feature : int { none=0,leftBend=1,rightBend=2,junction=3,leftTurn=4,rightTurn=5 };
enum class Match : int { noRoute=0,noFix=1,poorFix=2,offRoute=3,ambiguous=4,acquiring=5,ready=6,stale=7,arrived=8 };
struct Fix {
    double time=-infinity,latitude{},longitude{},accuracyM=infinity;
    double speedKmh{},bearingDegrees{},speedAccuracyMps=infinity;
    bool speedValid{},bearingValid{};
};
struct MacroContext {
    uint64_t routeId{},featureId{};
    Feature feature=Feature::none;
    Match match=Match::noRoute;
    double fixTime=-infinity,progressM{},distanceM=infinity;
    double headingChangeDegrees{},estimatedRadiusM=infinity;
    // Unknown unless independently populated from current, applicable map tags.
    double approachSpeedLimitKmh=infinity;
    bool valid{};
    int maneuverType{},maneuverShapeIndex=-1;
    double maneuverDistanceM=infinity;
    double gpsSpeedKmh=infinity,speedAccuracyMps=infinity,horizontalAccuracyM=infinity;
};
// Route construction allocates on a background worker. fix/context/evaluate/
// accepted do not allocate, perform I/O, call a router or run a neural model.
// Caller serializes access and supplies one monotonic clock.
class RouteCoach {
public:
    bool load(uint64_t routeId,const Coordinate*,size_t,const Maneuver*,size_t);
    void clear() noexcept;
    void invalidateFix() noexcept;
    bool fix(const Fix&) noexcept;
    MacroContext context(double now) const noexcept;
    Prompt evaluate(double now,bool braking,bool shock) const noexcept;
    void accepted(double now) noexcept;
    uint64_t routeId() const noexcept {return routeId_;}
    static const char* status(Match) noexcept;
private:
    struct Point {double x{},y{},s{};};
    struct Event {double s{},angle{},radius=infinity;Feature kind{};uint64_t id{};bool spoken{};};
    struct Step {double s{};int type{},shapeIndex{};};
    std::vector<Point> points_;
    std::vector<Event> events_;
    std::vector<Step> steps_;
    double latitude0_{},longitude0_{},cosLatitude_=1;
    uint64_t routeId_{};
    Fix fix_{};
    double progress_{},lastFix_=-infinity,acquiringSince_=-infinity;
    unsigned consistent_{};
    bool matched_{};
    Match match_=Match::noRoute;
    mutable uint64_t pending_{};
    double lastAccepted_=-infinity;
    Point pointAt(double s) const noexcept;
};

// Adapter-owned normalized values; never copy a provider SDK's private integer
// constants here. Unsupported maneuvers remain distinguishable and stay silent.
enum class TurnState : int { unknown=0,navigating=1,rerouting=2,stopped=3,arrived=4 };
enum class TurnKind : int { unknown=0,left=1,right=2,fork=3,ramp=4,merge=5,uTurn=6,roundabout=7,straight=8,destination=9 };
struct TurnInput {
    int provider{};
    uint64_t session{},routeEpoch{},sequence{};
    double receivedAt=-infinity;
    TurnState state=TurnState::unknown;
    int step=-1;
    TurnKind kind=TurnKind::unknown;
    double remainingM=infinity,remainingSeconds=infinity;
};
// Structured turn feed, independent of route geometry. Receipt freshness does
// not establish location freshness: a separately measured GPS fix is required.
class ProviderTurnCoach {
public:
    bool update(const TurnInput&) noexcept;
    void clear() noexcept;
    void unavailable() noexcept {available_=false;pending_=0;}
    void invalidateFix() noexcept;
    bool fix(const Fix&) noexcept;
    bool active() const noexcept {return active_;}
    MacroContext context(double now) const noexcept;
    Prompt evaluate(double now,bool braking,bool shock) const noexcept;
    void accepted(double now) noexcept;
    const char* status(double now) const noexcept;
private:
    TurnInput input_{};
    Fix fix_{};
    uint64_t eventId_{};
    bool active_{},retired_{},available_{};
    uint64_t acceptedSession_{},acceptedEpoch_{};
    int acceptedStep_=-1,highestStep_=-1;
    TurnKind acceptedKind_=TurnKind::unknown;
    mutable uint64_t pending_{};
    double lastFix_=-infinity,acquiringSince_=-infinity,lastAccepted_=-infinity;
    unsigned consistent_{};
};
} // namespace ld::navigation
