#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace ld::navigation {
// The next junction an instructor would prepare a learner for, on the road the
// car is on, WITHOUT a destination: GPS position + travel direction matched to
// road geometry (OpenStreetMap ways). Road classes follow OSM "highway" values.
struct JunctionAhead {
    bool valid{},roadEnds{};
    int64_t nodeId{},wayId{};
    double distanceM=std::numeric_limits<double>::infinity(),fixTime=-std::numeric_limits<double>::infinity();
    int branches{};
};
enum RoadClass : int { Motorway=0,Trunk=1,Primary=2,Secondary=3,Tertiary=4,Unclassified=5,Residential=6,LivingStreet=7,Service=8,Track=9 };
class JunctionScout {
public:
    void clear();
    // One road (ordered nodes with coordinates). Duplicate way ids are ignored.
    bool addWay(int64_t wayId,int roadClass,const int64_t* nodes,const double* latLon,size_t count);
    size_t wayCount() const noexcept {return ways_.size();}
    // Match a GPS fix and look ahead (<= 400 m) along the current road.
    JunctionAhead update(double time,double lat,double lon,double bearingDeg,bool bearingValid,double speedKmh,double accuracyM);
    // The last result while its fix is fresh (2.5 s), else invalid.
    JunctionAhead current(double now) const noexcept;
private:
    struct Node {double lat{},lon{};int branches{};};
    struct Way {int64_t id{};int cls{};std::vector<int64_t> nodes;};
    std::unordered_map<int64_t,Node> nodes_;
    std::vector<Way> ways_;
    std::unordered_set<int64_t> wayIds_;
    std::unordered_map<int64_t,std::vector<std::pair<size_t,size_t>>> nodeWays_;
    JunctionAhead ahead_{};
    double lastLat_{},lastLon_{},lastTime_=-std::numeric_limits<double>::infinity(),bearing_=std::numeric_limits<double>::quiet_NaN();
};
}
