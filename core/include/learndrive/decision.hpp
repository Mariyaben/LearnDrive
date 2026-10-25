#pragma once
#include "types.hpp"
namespace ld {
struct RoadShape {
    bool valid=false; int bend=0; double curvature=0;
    std::vector<float> points{}; // x,y pairs, near-to-far, normalized image coordinates.
    int source=0; // 1 road boundaries, 2 paired lane markings.
};
// Fits curvature after removing the straight-road perspective trend. This is
// image geometry, not a surveyed bend radius or a safe-speed estimate.
RoadShape roadShape(const LaneMap& map);
struct EngineSample {
    double time=-infinity,rpm=0,lowerRpm=0,upperRpm=0;
    int gear=0,maxGear=0;
    bool underLoad=false;
};
class DrivingCoach {
public:
    void reset();
    void road(double captureTime,const RoadShape& shape);
    // Cancel pending road cues without treating missing evidence as a clear road.
    void roadUnavailable(double captureTime);
    void engine(const EngineSample& sample) { engine_=sample; }
    const RoadShape& shape() const {return shape_;}
    double roadTime() const {return roadTime_;}
    Decision evaluate(const Telemetry& t,bool audioReady,bool manual,bool slopeEnabled=true);
    void accepted(Prompt prompt,double time);
private:
    struct Event {
        int candidate=0,active=0,count=0;
        bool spoken=false;
        double since=-1,last=-1,clearSince=-1;
        void update(double time,int value,bool valid,double dwell,int frames,double clearSeconds);
        bool pending() const {return active!=0&&candidate==active&&!spoken;}
    } slope_,bend_,shift_;
    int spokenSlopeBand_=0;
    bool seenSlope_=false,unavailableSpoken_=false;
    double unavailableSince_=-1,lastSlopeTime_=-1;
    EngineSample engine_;
    RoadShape shape_;
    double roadTime_=-infinity,lastSpoken_=-infinity;
};
// Image position is an attention-zone heuristic, not measured lane identity.
// Box size alone cannot justify an obstacle or slowing instruction.
class CameraCoach {
public:
    void start(double time);
    Decision evaluate(double now,double captureTime,const std::vector<Box>& boxes,bool ready,bool braking,bool shock,
        double speedKmh=0,bool speedValid=false,bool manual=false,double imageAspect=16.0/9);
    void accepted(Prompt prompt,double time);
    bool actorRelevant(int actorId,double now) const;
    bool newEncounterEligible(int actorId,int referenceId,double now) const;
private:
    bool started_=false,startupPending_=false;
    double startedAt_=0,lastSpoken_=-infinity;
    Prompt lastPrompt_=Prompt::none;
    double lastCapture_=-infinity,routineClearSince_=-1;
    int closeLevel_=0,spokenCloseLevel_=0,sceneBits_=0,spokenSceneBits_=0;
    struct Track {
        Box box;int id=0,hits=0,entryReferenceId=0;
        double since=0,last=0,lateralRate=0,riderUntil=-infinity,spokenAt=-infinity;
        bool spoken=false,entryPending=false;
    };
    std::vector<Track> attentionTracks_;
    int nextActorId_=1;
    int pendingActorId_=0,pendingEvidenceKind_=0;
    static bool relevant(const Track& track);
};
class DecisionEngine {
public:
    static double buffer(double speedKmh);
    // Select advice input without changing the measured fusion/GPS state.
    static Telemetry adviceTelemetry(Telemetry measured,bool requireGps,double manualSpeedKmh);
    static Telemetry videoTelemetry(Telemetry measured,double videoSpeedKmh,double testPitchDegrees);
    static Telemetry videoSpeedTelemetry(Telemetry measured,double videoSpeedKmh);
    Decision evaluate(const Telemetry& t,const Hazard& h,bool audioReady,bool allowGear=false) const;
    // Cooldown is committed only when a ready audio worker accepts the prompt.
    void accepted(double time) { lastPrompt_=time; }
    void reset() { lastPrompt_=-infinity; }
private:
    double lastPrompt_=-infinity;
};
}
