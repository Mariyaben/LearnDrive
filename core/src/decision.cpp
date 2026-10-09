#include "learndrive/decision.hpp"
namespace ld {
static RoadShape fitRoad(const double centers[5],double nearRow,double span,int source) {
    RoadShape result;
    const double a=(2*centers[0]-centers[1]-2*centers[2]-centers[3]+2*centers[4])/14;
    double mean=0,slope=0;for(int i=0;i<5;++i){mean+=centers[i]/5;slope+=(i-2)*centers[i]/10;}
    for(int i=0;i<5;++i)if(std::abs(centers[i]-(mean+slope*(i-2)+a*((i-2)*(i-2)-2)))>.025)return result;
    result.valid=true;result.source=source;result.curvature=8*a*std::pow(.32/span,2);
    if(std::abs(result.curvature)>=.065&&std::abs(result.curvature)<.40)result.bend=result.curvature<0?-1:1;
    for(int i=4;i>=0;--i){result.points.push_back(float(centers[i]));result.points.push_back(float(nearRow-span+span*i/4));}
    return result;
}
static RoadShape roadShapeAt(const LaneMap& map,double nearRow,double span) {
    RoadShape result;
    if(map.width<40||map.height<40||map.road.size()!=size_t(map.width)*map.height)return result;
    double centers[5]{},widths[5]{};
    // Trace a single bounded corridor from near to far. Split roads, clipping,
    // gaps and low-confidence boundaries do not establish a bend direction.
    for(int band=4;band>=0;--band) {
        int row=int((nearRow-span+span*band/4)*map.height),left=-1,right=-1;
        std::vector<std::pair<int,int>> runs;
        std::vector<bool> road(map.width);
        for(int x=0;x<map.width;++x) {
            double confidence=0;
            for(int dy=-1;dy<=1;++dy) {
                float p=map.road[(row+dy)*map.width+x];
                if(!std::isfinite(p)||p<0||p>1)return result;
                confidence+=p;
            }
            road[x]=confidence/3>.70;
        }
        for(int x=0;x<map.width;) {
            if(!road[x]) {++x;continue;}
            int start=x;while(x<map.width&&road[x])++x;
            if(x-start>=std::max(3,int(.06*map.width)))runs.push_back({start,x});
        }
        if(runs.empty()||runs.size()>2)return result;
        left=runs.front().first;right=runs.back().second;
        // A small central hole from a car/segmentation gap need not split an
        // otherwise coherent road. Wide gaps and forks remain ambiguous.
        if(runs.size()==2) {
            int gap=runs[1].first-runs[0].second;
            if(gap>.08*map.width||gap>.25*(right-left))return result;
        }
        if(left<2||right>map.width-2)return result;
        centers[band]=(left+right)*.5/map.width;widths[band]=double(right-left)/map.width;
        if(band<4&&(std::abs(centers[band]-centers[band+1])>.20||widths[band]>widths[band+1]*1.35))return result;
    }
    if(widths[4]<.15||centers[4]<.25||centers[4]>.75)return result;
    return fitRoad(centers,nearRow,span,1);
}
static RoadShape laneShapeAt(const LaneMap& map,double nearRow,double span) {
    double centers[5]{},widths[5]{};
    for(int band=4;band>=0;--band) {
        const int row=int((nearRow-span+span*band/4)*map.height);
        std::vector<double> markers;
        for(int x=0;x<map.width;) {
            auto marked=[&](int column) {float p=0;for(int dy=-2;dy<=2;++dy)p=std::max(p,map.lane[(row+dy)*map.width+column]);return p>.65f;};
            if(!marked(x)){++x;continue;}
            int start=x;while(x<map.width&&marked(x))++x;
            if(x-start<=.10*map.width)markers.push_back((start+x)*.5/map.width);
        }
        const double expected=band==4?.5:centers[band+1];double best=infinity;
        for(size_t i=1;i<markers.size();++i) {
            double left=markers[i-1],right=markers[i],width=right-left,center=(left+right)/2;
            if(width<.06||width>.8||std::abs(center-expected)>.18)continue;
            if(band==4&&!(left<.47&&right>.53&&width>=.15))continue;
            if(band<4&&width>widths[band+1]*1.35)continue;
            int supported=0;
            for(double fraction:{.25,.5,.75})if(map.inRoad(float(left+fraction*width),float(nearRow-span+span*band/4)))++supported;
            if(supported<2)continue;
            if(std::abs(center-expected)<best){best=std::abs(center-expected);centers[band]=center;widths[band]=width;}
        }
        if(!std::isfinite(best))return {};
    }
    return fitRoad(centers,nearRow,span,2);
}
RoadShape roadShape(const LaneMap& map) {
    if(map.width<40||map.height<40||map.road.size()!=size_t(map.width)*map.height)return {};
    for(float p:map.road)if(!std::isfinite(p)||p<0||p>1)return {};
    // Near road edges often fall outside the camera. Move the fitting window
    // farther up the image, while still requiring five bounded valid bands.
    for(double nearRow:{.82,.78,.74,.70,.66})for(double span:{.32,.24}) {
        auto shape=roadShapeAt(map,nearRow,span);if(shape.valid)return shape;
    }
    // Paired markings supply a bounded corridor when road edges are cropped.
    if(map.lane.size()==map.road.size()) {
        for(float p:map.lane)if(!std::isfinite(p)||p<0||p>1)return {};
        for(double nearRow:{.82,.78,.74,.70,.66})for(double span:{.32,.24}) {
            auto shape=laneShapeAt(map,nearRow,span);if(shape.valid)return shape;
        }
    }
    return {};
}
void DrivingCoach::Event::update(double time,int value,bool valid,double dwell,int frames,double clearSeconds) {
    if(!std::isfinite(time)||time<0||time<=last)return;
    const bool gap=last<0||time-last>.4;last=time;
    if(!valid) {candidate=0;since=-1;count=0;clearSince=-1;return;}
    if(value==0) {
        candidate=0;since=-1;count=0;
        if(clearSince<0||gap)clearSince=time;
        if(time-clearSince>=clearSeconds) {active=0;spoken=false;}
        return;
    }
    clearSince=-1;
    if(candidate!=value||gap) {candidate=value;since=time;count=1;}else ++count;
    if(active!=value&&time-since>=dwell&&count>=frames) {active=value;spoken=false;}
}
void DrivingCoach::reset() { *this=DrivingCoach{}; }
void DrivingCoach::road(double time,const RoadShape& shape) {
    if(!std::isfinite(time)||time<0||time<=roadTime_)return;
    shape_=shape;roadTime_=time;
    bend_.update(time,shape.bend,shape.valid&&shape.bend>=-1&&shape.bend<=1,.4,3,2);
}
Decision DrivingCoach::evaluate(const Telemetry& t,bool ready,bool manual,bool slopeEnabled,bool clearPath) {
    Decision d;
    if(!std::isfinite(t.time)||t.time<0){d.reason="invalid coaching time";return d;}
    const bool pitch=slopeEnabled&&t.calibrated&&t.pitchValid&&std::isfinite(t.pitchDegrees)&&std::abs(t.pitchDegrees)<=30;
    const int rawGrade=pitch?slopeBand(t.pitchDegrees):0;
    int grade=rawGrade;
    if(pitch&&slope_.active!=0&&t.pitchDegrees*slope_.active>0&&std::abs(rawGrade)<std::abs(slope_.active)) {
        const double boundaries[]{0,2,5,8,12};
        if(std::abs(t.pitchDegrees)>=boundaries[std::abs(slope_.active)]-.5)grade=slope_.active;
    }
    slope_.update(t.time,grade,pitch,1.5,3,2);
    if(pitch&&slope_.active==0&&grade==0){spokenSlopeBand_=0;slopeWasMaintain_=false;}
    if(pitch&&slopeWasMaintain_&&t.speedValid&&std::isfinite(t.speedKmh)&&
        (t.speedKmh>lowAdviceSpeedKmh||(std::isfinite(t.longitudinalAcceleration)&&t.longitudinalAcceleration>.2))) {
        slope_.spoken=false;spokenSlopeBand_=0;slopeWasMaintain_=false;
    }
    if(t.time>lastSlopeTime_) {
        if(pitch){seenSlope_=true;unavailableSince_=-1;unavailableSpoken_=false;}
        else if(slopeEnabled&&(seenSlope_||(t.calibrated&&t.pitchValid))) {
            if(unavailableSince_<0||t.time-lastSlopeTime_>.4)unavailableSince_=t.time;
        } else unavailableSince_=-1;
        lastSlopeTime_=t.time;
    }
    const double roadAge=t.time-roadTime_;
    const bool roadFresh=shape_.valid&&roadAge>=0&&roadAge<maxVisionAgeSeconds;
    const double engineAge=t.time-engine_.time;
    const bool engine=manual&&engineAge>=0&&engineAge<1&&engine_.gear>=1&&engine_.gear<=engine_.maxGear&&engine_.maxGear<=10&&
        std::isfinite(engine_.rpm)&&engine_.rpm>=500&&engine_.rpm<=12000&&std::isfinite(engine_.lowerRpm)&&
        std::isfinite(engine_.upperRpm)&&engine_.lowerRpm>=500&&engine_.upperRpm>engine_.lowerRpm+500&&engine_.upperRpm<=12000;
    int shift=0;
    if(engine&&engine_.underLoad) {
        if(engine_.rpm<engine_.lowerRpm&&engine_.gear>1)shift=-engine_.gear;
        else if(engine_.rpm>engine_.upperRpm&&engine_.gear<engine_.maxGear&&(!pitch||grade>=0)&&(!roadFresh||shape_.bend==0))shift=engine_.gear;
    }
    shift_.update(t.time,shift,engine,.8,3,2);
    if(!ready) {d.reason="offline audio not ready";return d;}
    if(t.braking||t.hardBraking||t.shock) {d.reason="driver braking or phone shock";return d;}
    if(engine&&shift_.pending()) {d.prompt=shift_.active>0?Prompt::shiftUp:Prompt::shiftDown;d.reason="fresh gear and RPM outside configured band under load";}
    else if(roadFresh&&bend_.pending()) {
        const bool fast=t.speedValid&&std::isfinite(t.speedKmh)&&t.speedKmh>lowAdviceSpeedKmh;
        d.prompt=bend_.active<0?(manual?Prompt::bendLeftGear:fast?Prompt::bendLeft:Prompt::bendLeftQuiet):
            (manual?Prompt::bendRightGear:fast?Prompt::bendRight:Prompt::bendRightQuiet);
        d.reason="sustained curved road corridor";
    }
    else if(pitch&&slope_.pending()&&rawGrade==slope_.active&&
        (spokenSlopeBand_*slope_.active<=0||std::abs(slope_.active)>std::abs(spokenSlopeBand_))) {
        const bool stationary=t.speedValid&&std::isfinite(t.speedKmh)&&t.speedKmh<=.5;
        if(stationary){d.reason="vehicle stationary; slope action cues quiet";return d;}
        const bool controlled=clearPath&&roadFresh&&shape_.bend==0&&t.speedValid&&std::isfinite(t.speedKmh)&&t.speedKmh>.5&&t.speedKmh<=lowAdviceSpeedKmh&&
            std::isfinite(t.longitudinalAcceleration)&&std::abs(t.longitudinalAcceleration)<=.2;
        if(controlled)d.prompt=Prompt::maintainSpeed;
        else switch(slope_.active) {
        case 1:d.prompt=Prompt::gentleUp;break;
        case 2:d.prompt=manual?Prompt::hill:Prompt::uphill;break;
        case 3:d.prompt=Prompt::steepUp;break;
        case 4:d.prompt=Prompt::verySteepUp;break;
        case -1:d.prompt=Prompt::gentleDown;break;
        case -2:d.prompt=manual?Prompt::downhillGear:Prompt::downhill;break;
        case -3:d.prompt=manual?Prompt::steepDownGear:Prompt::steepDown;break;
        case -4:d.prompt=Prompt::verySteepDown;break;
        }
        pendingSlopePrompt_=d.prompt;d.reason="sustained slope band from normalized phone plane";
    } else if(slopeEnabled&&unavailableSince_>=0&&t.time-unavailableSince_>=1.5&&!unavailableSpoken_) {
        d.prompt=Prompt::slopeUnavailable;d.reason="slope reading outside supported range or unavailable";
    } else {d.reason="watching for a new slope band, bend or necessary shift";return d;}
    if(t.time<lastSpoken_||t.time-lastSpoken_<6) {d.prompt=Prompt::none;d.reason="driving guidance spacing";}
    return d;
}
void DrivingCoach::accepted(Prompt p,double time) {
    if(!std::isfinite(time)||time<0)return;
    bool valid=false;
    const int band=slopePromptBand(p);
    if(band!=0||(p==Prompt::maintainSpeed&&pendingSlopePrompt_==p)) {
        valid=slope_.pending()&&(band==slope_.active||p==Prompt::maintainSpeed);
        if(valid){slope_.spoken=true;spokenSlopeBand_=slope_.active;slopeWasMaintain_=p==Prompt::maintainSpeed;}
        if(valid)lastSpoken_=time;
        return;
    }
    switch(p) {
    case Prompt::slopeUnavailable:valid=unavailableSince_>=0&&!unavailableSpoken_;if(valid)unavailableSpoken_=true;break;
    case Prompt::bendLeft:case Prompt::bendRight:case Prompt::bendLeftQuiet:case Prompt::bendRightQuiet:case Prompt::bendLeftGear:case Prompt::bendRightGear:
        valid=bend_.pending()&&bend_.active==((p==Prompt::bendLeft||p==Prompt::bendLeftQuiet||p==Prompt::bendLeftGear)?-1:1);if(valid)bend_.spoken=true;break;
    case Prompt::shiftUp:case Prompt::shiftDown:valid=shift_.pending()&&((shift_.active>0)==(p==Prompt::shiftUp));if(valid)shift_.spoken=true;break;
    default:break;
    }
    if(valid)lastSpoken_=time;
}
void CameraCoach::start(double time) {
    started_=std::isfinite(time)&&time>=0;startupPending_=started_;startedAt_=time;
    lastSpoken_=-infinity;lastPrompt_=Prompt::none;
    lastCapture_=-infinity;closeClearSince_=routineClearSince_=-1;
    closeLevel_=spokenCloseLevel_=sceneBits_=spokenSceneBits_=0;
}
void CameraCoach::accepted(Prompt prompt,double time) {
    if(started_&&priority(prompt)>0&&std::isfinite(time)&&time>=startedAt_) {
        startupPending_=false;lastPrompt_=prompt;lastSpoken_=time;
        if(prompt==Prompt::close||prompt==Prompt::closeGear||prompt==Prompt::closeQuiet||prompt==Prompt::closeGearQuiet||prompt==Prompt::imageClose||prompt==Prompt::imageCloseGear||
           prompt==Prompt::emergency||prompt==Prompt::nearbyCollision) {
            spokenCloseLevel_=std::max(spokenCloseLevel_,closeLevel_);spokenSceneBits_|=sceneBits_;
        } else if(prompt==Prompt::roadUser||prompt==Prompt::roadUserQuiet)spokenSceneBits_|=2;
        else if(prompt==Prompt::traffic)spokenSceneBits_|=1;
    }
}
Decision CameraCoach::evaluate(double time,double captureTime,const std::vector<Box>& boxes,bool ready,bool braking,bool shock,double speed,bool speedValid,bool manual) {
    Decision d;
    if(!started_||!std::isfinite(time)||time<startedAt_) {d.reason="camera not running";return d;}
    if(!ready) {d.reason="offline audio not ready";return d;}
    const double age=time-captureTime;
    const bool fresh=std::isfinite(captureTime)&&captureTime>=startedAt_&&age>=0&&age<maxVisionAgeSeconds;
    const bool knownSpeed=speedValid&&std::isfinite(speed)&&speed>=0&&speed<=160;
    const bool fast=knownSpeed&&speed>lowAdviceSpeedKmh;
    bool traffic=false,roadUser=false,close=false,critical=false,massive=false,approaching=false;
    if(fresh)for(const auto& b:boxes) {
        const bool category=b.category==0||b.category==1||b.category==2||b.category==3||b.category==5||b.category==7;
        if(!category||!std::isfinite(b.score)||b.score<.4||b.score>1||!std::isfinite(b.x)||!std::isfinite(b.y)||
           !std::isfinite(b.w)||!std::isfinite(b.h)||b.w<=0||b.h<=0||b.x<0||b.y<0||b.x+b.w>1.001||b.y+b.h>1.001)continue;
        const float area=b.w*b.h,foot=b.y+b.h;
        if(area<.001f)continue;
        if(b.category==0||b.category==1||b.category==3)roadUser=true;else traffic=true;
        const bool path=b.x<.66f&&b.x+b.w>.34f&&foot>.5f;
        // Large central occupancy gives a caution without requiring expansion
        // or a road pixel that may be hidden by the obstacle itself.
        close=close||(path&&area>=.04f&&(foot>=.6f||area>=.15f)&&(b.w>=.22f||b.h>=.4f));
        massive=massive||(path&&area>=.25f&&foot>=.8f);
        critical=critical||(path&&std::isfinite(b.confidence)&&b.confidence>=.65&&b.confidence<=1&&
            std::isfinite(b.tti)&&b.tti>0&&b.tti-age<1.2);
        approaching=approaching||(path&&std::isfinite(b.confidence)&&b.confidence>=.65&&b.confidence<=1&&
            std::isfinite(b.tti)&&b.tti>0&&b.tti-age<DecisionEngine::buffer(knownSpeed?speed:30));
    }
    if(fresh) {
        sceneBits_=(traffic?1:0)|(roadUser?2:0);closeLevel_=close?(massive?2:1)+(fast?2:0)+(approaching?4:0):0;
        if(captureTime>lastCapture_) {
            const bool gap=captureTime-lastCapture_>.4;
            if(close)closeClearSince_=-1;
            else {if(closeClearSince_<0||gap)closeClearSince_=captureTime;if(captureTime-closeClearSince_>=2)spokenCloseLevel_=0;}
            if(sceneBits_!=0)routineClearSince_=-1;
            else {if(routineClearSince_<0||gap)routineClearSince_=captureTime;if(captureTime-routineClearSince_>=2)spokenSceneBits_=0;}
            lastCapture_=captureTime;
        }
    }
    if(!braking&&!shock&&critical) {
        d.prompt=knownSpeed&&speed<=.5?Prompt::nearbyCollision:Prompt::emergency;d.reason="rapid approach in camera path";
    }
    else if(!braking&&!shock&&close&&closeLevel_>spokenCloseLevel_) {
        d.prompt=fast?((massive||approaching)?(manual?Prompt::closeGear:Prompt::close):(manual?Prompt::imageCloseGear:Prompt::imageClose)):
            (manual&&knownSpeed&&speed>.5?Prompt::closeGearQuiet:Prompt::closeQuiet);
        d.reason="large obstacle overlaps the path ahead";
    }
    else if(startupPending_) {d.prompt=Prompt::camera;d.reason="camera guidance started";return d;}
    else if(braking||shock) {d.reason=braking?"driver braking":"IMU shock";return d;}
    else if(close) {d.reason="obstacle already announced; watching for increased risk";return d;}
    else if(roadUser&&!(spokenSceneBits_&2)) {d.prompt=fast?Prompt::roadUser:Prompt::roadUserQuiet;d.reason="road user visible ahead";}
    else if(traffic&&!(spokenSceneBits_&1)) {d.prompt=Prompt::traffic;d.reason="traffic visible ahead";}
    else {d.reason=fresh?"watching the road; no road user detected":"waiting for a fresh camera frame";return d;}
    // Higher urgency interrupts routine speech immediately. Routine messages
    // do not consume the tactical decision engine's collision cooldown.
    if(lastPrompt_!=Prompt::camera&&priority(d.prompt)<=priority(lastPrompt_)) {
        const bool collision=d.prompt==Prompt::emergency||d.prompt==Prompt::nearbyCollision;
        const bool increasing=close&&spokenCloseLevel_>0&&closeLevel_>spokenCloseLevel_;
        const double gap=d.prompt==lastPrompt_&&!collision?8:3.5;
        if(!increasing&&time-lastSpoken_<gap) {d.prompt=Prompt::none;d.reason="camera guidance cooldown";}
    }
    return d;
}
double DecisionEngine::buffer(double speed) { return std::clamp(3+(speed-30)*4/50,3.0,7.0); }
Telemetry DecisionEngine::adviceTelemetry(Telemetry measured,bool requireGps,double manualSpeed) {
    if(measured.speedValid&&std::isfinite(measured.speedKmh)&&measured.speedKmh>=0)return measured;
    measured.speedValid=false;
    if(!requireGps&&std::isfinite(manualSpeed)&&manualSpeed>=0&&manualSpeed<=160) {
        measured.speedKmh=manualSpeed;measured.speedValid=true;
    }
    return measured;
}
Telemetry DecisionEngine::videoTelemetry(Telemetry measured,double speed,double pitch) {
    measured.speedKmh=speed;measured.speedValid=std::isfinite(speed)&&speed>=0&&speed<=160;
    measured.pitchDegrees=pitch;measured.pitchValid=std::isfinite(pitch)&&std::abs(pitch)<=20;
    measured.calibrated=measured.pitchValid;measured.calibrationProgress=1;
    measured.longitudinalAcceleration=0;measured.braking=measured.hardBraking=false;
    return measured; // Keep the clock and shock flag; never modify real fusion.
}
Telemetry DecisionEngine::videoSpeedTelemetry(Telemetry measured,double speed) {
    measured.speedKmh=speed;measured.speedValid=std::isfinite(speed)&&speed>=0&&speed<=160;
    return measured; // Phone slope/calibration/braking stay measured, speed is a video setting.
}
Decision DecisionEngine::evaluate(const Telemetry& t,const Hazard& h,bool ready,bool gear) const {
    (void)gear; // Retained for ABI compatibility; DrivingCoach handles gears.
    Decision d; d.bufferSeconds=buffer(t.speedKmh);
    if(!std::isfinite(t.time)||!std::isfinite(t.speedKmh)||t.speedKmh<0||!t.speedValid||!t.calibrated||!t.pitchValid) { d.reason="telemetry unavailable"; return d; }
    if(t.braking||t.hardBraking) { d.reason="driver braking"; return d; }
    if(t.shock) { d.reason="IMU shock"; return d; }
    if(!ready) { d.reason="offline audio not ready"; return d; }
    if(t.time<lastPrompt_||t.time-lastPrompt_<3.5) { d.reason="cooldown"; return d; }
    const double age=t.time-h.time;
    bool fresh=std::isfinite(h.time)&&age>=0&&age<maxVisionAgeSeconds;
    bool hazard=fresh&&h.inPath&&std::isfinite(h.confidence)&&h.confidence>=.65&&h.confidence<=1&&std::isfinite(h.tti)&&h.tti>0;
    // TTI describes the capture instant, so elapsed processing time is deducted.
    const double remaining=hazard?std::max(.01,h.tti-age):infinity;
    if(hazard&&remaining<1.2&&t.speedKmh>.5) { d.prompt=Prompt::emergency; d.reason="critical approach"; }
    else if(hazard&&remaining<d.bufferSeconds&&t.speedKmh>50) { d.prompt=Prompt::ease; d.reason="approaching hazard"; }
    else if(hazard&&remaining<d.bufferSeconds&&t.speedKmh>lowAdviceSpeedKmh) { d.prompt=Prompt::brake; d.reason="approaching hazard"; }
    // Gear/slope episodes belong to DrivingCoach; speed alone cannot establish
    // the vehicle's current gear or whether a shift is needed.
    else d.reason=!fresh&&h.inPath ? "vision result expired":"no actionable condition";
    return d;
}
}
