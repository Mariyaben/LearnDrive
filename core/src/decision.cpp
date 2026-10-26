#include "learndrive/decision.hpp"
#include "learndrive/vision.hpp"
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
void DrivingCoach::roadUnavailable(double time) {
    if(!std::isfinite(time)||time<0||time<roadTime_)return;
    shape_={};roadTime_=time;
    bend_.candidate=0;bend_.since=-1;bend_.count=0;bend_.clearSince=-1;bend_.last=time;
}
Decision DrivingCoach::evaluate(const Telemetry& t,bool ready,bool manual,bool slopeEnabled) {
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
    if(pitch&&slope_.active==0&&grade==0)spokenSlopeBand_=0;
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
        const bool steadyLowSpeed=t.speedValid&&std::isfinite(t.speedKmh)&&t.speedKmh>.5&&t.speedKmh<=lowAdviceSpeedKmh&&
            std::isfinite(t.longitudinalAcceleration)&&std::abs(t.longitudinalAcceleration)<=.2;
        // A measured steady response can make another slope instruction
        // unnecessary. It cannot establish that maintaining speed is safe;
        // empty detections and a straight-road fit do not supply that evidence.
        // Keep the pending lesson available if motion subsequently changes.
        if(steadyLowSpeed){d.reason="steady low-speed motion; no additional slope action";return d;}
        switch(slope_.active) {
        case 1:d.prompt=Prompt::gentleUp;break;
        case 2:d.prompt=manual?Prompt::hill:Prompt::uphill;break;
        case 3:d.prompt=Prompt::steepUp;break;
        case 4:d.prompt=Prompt::verySteepUp;break;
        case -1:d.prompt=Prompt::gentleDown;break;
        case -2:d.prompt=manual?Prompt::downhillGear:Prompt::downhill;break;
        case -3:d.prompt=manual?Prompt::steepDownGear:Prompt::steepDown;break;
        case -4:d.prompt=Prompt::verySteepDown;break;
        }
        d.reason="sustained slope band from normalized phone plane";
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
    if(band!=0) {
        valid=slope_.pending()&&band==slope_.active;
        if(valid){slope_.spoken=true;spokenSlopeBand_=slope_.active;}
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
    *this=CameraCoach{};
    started_=std::isfinite(time)&&time>=0;startupPending_=started_;startedAt_=time;
}
bool CameraCoach::relevant(const Track& tr) {
    const auto& b=tr.box;const double center=b.x+b.w*.5,foot=b.y+b.h;
    const bool vulnerable=b.category==0||b.category==1||b.category==3;
    const bool enters=vulnerable&&std::abs(tr.lateralRate)<.6&&center+.7*tr.lateralRate>.3&&center+.7*tr.lateralRate<.7;
    const double margin=b.category==0?.25:.18;
    return foot>.5&&((center>.32&&center<.68)||(vulnerable&&center>margin&&center<1-margin&&enters));
}
bool CameraCoach::actorRelevant(int id,double now) const {
    if(!std::isfinite(now)||now<lastCapture_||now-lastCapture_>=maxVisionAgeSeconds)return false;
    for(const auto& tr:attentionTracks_)if(tr.id==id&&tr.last==lastCapture_)return tr.riderUntil<lastCapture_&&relevant(tr);
    return false;
}
bool CameraCoach::newEncounterEligible(int id,int reference,double now) const {
    if(reference<=0||reference==id||!actorRelevant(id,now))return false;
    for(const auto& tr:attentionTracks_)if(tr.id==id)
        return !tr.spoken&&tr.entryPending&&tr.entryReferenceId==reference&&tr.hits>=2&&lastCapture_-tr.since>=.12;
    return false;
}
bool CameraCoach::actorBox(int id,double now,Box& out) const {
    if(!actorRelevant(id,now))return false;
    for(const auto& track:attentionTracks_)if(track.id==id){out=track.box;return true;}
    return false;
}
void CameraCoach::accepted(Prompt prompt,double time) {
    if(!started_||priority(prompt)<=0||!std::isfinite(time)||time<startedAt_)return;
    startupPending_=false;lastPrompt_=prompt;lastSpoken_=time;
    if(prompt==Prompt::vehicleSpace||prompt==Prompt::roadUserQuiet)spokenSceneBits_|=1;
    if(prompt==Prompt::riderSpace)spokenSceneBits_|=2;
    if(prompt==Prompt::pedestrianSpace)spokenSceneBits_|=4;
    const bool attention=prompt==Prompt::pedestrianSpace||prompt==Prompt::riderSpace||prompt==Prompt::vehicleSpace||prompt==Prompt::roadUserQuiet;
    if(attention||prompt==Prompt::keepSafeGap)
        for(auto& track:attentionTracks_)if(track.id==pendingActorId_){track.spoken=true;track.spokenAt=time;track.entryPending=false;}
    if(prompt==Prompt::keepSafeGap||prompt==Prompt::emergency||prompt==Prompt::nearbyCollision||(attention&&pendingEvidenceKind_==2)) {
        spokenCloseLevel_=std::max(spokenCloseLevel_,closeLevel_);spokenSceneBits_|=sceneBits_;
    }
}
void CameraCoach::acceptedActor(Prompt prompt,double time,int actorId,int evidenceKind) {
    if(actorId<=0||!actorRelevant(actorId,time))return;
    const int previousActor=pendingActorId_,previousKind=pendingEvidenceKind_;
    pendingActorId_=actorId;pendingEvidenceKind_=evidenceKind;
    accepted(prompt,time);
    pendingActorId_=previousActor;pendingEvidenceKind_=previousKind;
}
Decision CameraCoach::evaluate(double time,double captureTime,const std::vector<Box>& boxes,bool ready,bool braking,bool shock,
        double speed,bool speedValid,bool manual,double imageAspect) {
    (void)manual; // A camera detection cannot establish gear, RPM or engine strain.
    Decision d;
    pendingActorId_=pendingEvidenceKind_=0;
    if(!started_||!std::isfinite(time)||time<startedAt_){d.reason="camera not running";return d;}
    if(!ready){d.reason="offline audio not ready";return d;}
    const double age=time-captureTime;
    const bool fresh=std::isfinite(captureTime)&&captureTime>=startedAt_&&age>=0&&age<maxVisionAgeSeconds;
    if(!fresh){
        if(startupPending_){d.prompt=Prompt::camera;d.reason="camera guidance started";}
        else d.reason="waiting for a fresh camera frame";
        return d;
    }
    if(!std::isfinite(imageAspect)||imageAspect<.3||imageAspect>4){d.reason="image geometry unavailable";return d;}
    // Track distinct observations, never repeated 100 Hz evaluations of one box.
    if(captureTime>lastCapture_) {
        std::vector<Track> next;std::vector<bool> used(attentionTracks_.size());
        for(const auto& b:boxes) {
            const bool vulnerable=b.category==0||b.category==1||b.category==3;
            const bool vehicle=b.category==2||b.category==5||b.category==7;
            if((!vulnerable&&!vehicle)||!std::isfinite(b.score)||b.score<(vulnerable?.5:.6)||b.score>1||
               !std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.w)||!std::isfinite(b.h)||
               b.x<0||b.y<0||b.w<=0||b.h<=0||b.x+b.w>1.001||b.y+b.h>1.001||b.w*b.h<.002)continue;
            const double aspect=b.w*imageAspect/b.h;
            // Plausibility only: preserve the raw detector box for review. Wide
            // road/guardrail hallucinations must not become driving instructions.
            const double minimum=b.category==0?.12:b.category==2?.6:.2;
            const double maximum=b.category==0?1.6:b.category==2?3.3:5.;
            if(aspect<minimum||aspect>maximum)continue;
            Track track;track.box=b;track.hits=1;track.since=track.last=captureTime;
            int best=-1;float overlap=.25f;
            for(size_t i=0;i<attentionTracks_.size();++i)if(!used[i]&&attentionTracks_[i].box.category==b.category&&
                captureTime-attentionTracks_[i].last>0&&captureTime-attentionTracks_[i].last<=.75) {
                const float value=iou(b,attentionTracks_[i].box);
                // Reassociation after a missed observation needs tighter shape
                // overlap than continuous tracking. Retained history is identity
                // only; it never counts as a fresh observation or dwell.
                const bool missing=attentionTracks_[i].last!=lastCapture_;
                if(value>overlap&&(!missing||value>.5f)){overlap=value;best=int(i);}
            }
            if(best>=0) {
                used[best]=true;const auto& old=attentionTracks_[best];const double dt=captureTime-old.last;
                track.id=old.id;
                track.spoken=old.spoken;track.spokenAt=old.spokenAt;track.entryPending=old.entryPending;track.entryReferenceId=old.entryReferenceId;
                track.riderUntil=old.riderUntil;
                if(dt>0&&dt<maxVisionAgeSeconds&&old.last==lastCapture_) {
                    track.hits=old.hits+1;track.since=old.since;
                    track.lateralRate=((b.x+b.w*.5)-(old.box.x+old.box.w*.5))/dt;
                    const double before=old.box.x+old.box.w*.5,center=b.x+b.w*.5;
                    if(center>.32&&center<.68&&(before<=.32||before>=.68)&&!track.spoken)track.entryPending=true;
                }
            } else track.id=nextActorId_++;
            if(!relevant(track)){track.entryPending=false;track.entryReferenceId=0;}
            next.push_back(track);if(next.size()==64)break;
        }
        // Overlap alone also occurs when a pedestrian stands behind a bike.
        // Associate a rider body only after two distinct, continuous frames
        // show both boxes moving together. Static/ambiguous persons stay visible
        // to attention selection; brief class loss preserves only proven links.
        for(auto& person:next)if(person.box.category==0)for(const auto& bike:next)
            if((bike.box.category==1||bike.box.category==3)&&bike.box.y>=person.box.y&&
               bike.box.y<person.box.y+person.box.h&&bike.box.y+bike.box.h>person.box.y+person.box.h+.04&&
               std::abs(person.box.x+person.box.w*.5-bike.box.x-bike.box.w*.5)<std::max(person.box.w,bike.box.w)*.25) {
                const Track* oldPerson=nullptr;const Track* oldBike=nullptr;
                for(const auto& old:attentionTracks_)if(old.last==lastCapture_&&captureTime-old.last<maxVisionAgeSeconds) {
                    if(old.id==person.id)oldPerson=&old;if(old.id==bike.id)oldBike=&old;
                }
                if(!oldPerson||!oldBike)continue;
                const double px=person.box.x+person.box.w*.5-oldPerson->box.x-oldPerson->box.w*.5;
                const double py=person.box.y+person.box.h*.5-oldPerson->box.y-oldPerson->box.h*.5;
                const double bx=bike.box.x+bike.box.w*.5-oldBike->box.x-oldBike->box.w*.5;
                const double by=bike.box.y+bike.box.h*.5-oldBike->box.y-oldBike->box.h*.5;
                if(std::hypot(px,py)>=.01&&std::hypot(bx,by)>=.01&&std::hypot(px-bx,py-by)<.012)
                    person.riderUntil=captureTime+.75;
            }
        // Distinct encounter proof requires two independently visible actors.
        // A new tracker ID after occlusion cannot alone defeat phrase dedup.
        for(auto& tr:next)if(tr.entryPending&&!tr.spoken) {
            double latest=-infinity;
            for(const auto& other:next)if(other.spoken&&other.spokenAt>latest&&other.id!=tr.id&&
                other.box.category==tr.box.category&&other.riderUntil<captureTime&&relevant(other)&&
                (tr.box.x+tr.box.w+.01f<=other.box.x||other.box.x+other.box.w+.01f<=tr.box.x)) {
                tr.entryReferenceId=other.id;latest=other.spokenAt;
            }
        }
        for(size_t i=0;i<attentionTracks_.size()&&next.size()<128;++i)
            if(!used[i]&&captureTime-attentionTracks_[i].last<=.75)next.push_back(attentionTracks_[i]);
        attentionTracks_=std::move(next);
        const bool gap=captureTime-lastCapture_>.4;
        lastCapture_=captureTime;sceneBits_=0;closeLevel_=0;
        // Only confirmed empty/relevance-free observations rearm an episode.
        // A missing frame or a long processing gap never proves a clear road.
        for(const auto& tr:attentionTracks_) {
            if(tr.last!=captureTime||tr.riderUntil>=captureTime||!relevant(tr))continue;
            const bool vulnerable=tr.box.category==0||tr.box.category==1||tr.box.category==3;
            sceneBits_|=tr.box.category==0?4:vulnerable?2:1;
        }
        if(sceneBits_)routineClearSince_=-1;
        else {if(routineClearSince_<0||gap)routineClearSince_=captureTime;
            if(captureTime-routineClearSince_>=2){spokenSceneBits_=0;spokenCloseLevel_=0;}}
    }
    const bool knownSpeed=speedValid&&std::isfinite(speed)&&speed>=0&&speed<=160;
    const bool stationary=knownSpeed&&speed<=.5;
    const Track* selected=nullptr;int selectedRank=0,kind=0;
    for(const auto& tr:attentionTracks_) {
        if(tr.last!=lastCapture_||tr.riderUntil>=lastCapture_||!relevant(tr))continue;
        const auto& b=tr.box;const double center=b.x+b.w*.5,foot=b.y+b.h;
        const bool vulnerable=b.category==0||b.category==1||b.category==3;
        const bool central=center>.32&&center<.68&&foot>.5;
        const bool optical=central&&std::isfinite(b.confidence)&&b.confidence>=.65&&b.confidence<=1&&
            std::isfinite(b.tti)&&b.tti>0;
        const bool critical=optical&&b.tti-age<1.2;
        const bool approach=optical&&b.tti-age<DecisionEngine::buffer(knownSpeed?speed:30);
        const int bit=b.category==0?4:vulnerable?2:1;
        // Routine vehicle presence deserves less interruption than a vulnerable
        // road user. Wait for a sustained encounter before a general gap lesson.
        const bool entry=!tr.spoken&&tr.entryPending;
        const bool sustained=tr.hits>=((vulnerable||entry)?2:3)&&captureTime-tr.since>=((vulnerable||entry)?.12:1.0);
        const int rank=critical?100:!stationary&&approach&&spokenCloseLevel_<1?80:
            !stationary&&sustained&&(!(spokenSceneBits_&bit)||entry)?(b.category==0?30:vulnerable?20:entry?15:10):0;
        if(rank>selectedRank){selected=&tr;selectedRank=rank;kind=critical?3:approach?2:1;}
    }
    if(braking||shock){d.reason=braking?"driver braking":"IMU shock";return d;}
    if(selectedRank>=100) {
        d.prompt=stationary?Prompt::nearbyCollision:Prompt::emergency;
        d.reason="optically confirmed critical approach in forward attention zone";closeLevel_=2;
    } else if(startupPending_){d.prompt=Prompt::camera;d.reason="camera guidance started";return d;}
    else if(selectedRank>=80) {
        const bool lead=knownSpeed&&speed>.5&&time>=selected->box.leadTime&&time-selected->box.leadTime<maxVisionAgeSeconds;
        d.prompt=lead?Prompt::keepSafeGap:selected->box.category==0?Prompt::pedestrianSpace:
            (selected->box.category==1||selected->box.category==3)?Prompt::riderSpace:Prompt::roadUserQuiet;
        d.reason=lead?"optical approach with independently matched trajectory-supported lead":
            "optically approaching road user; following relationship unverified";closeLevel_=1;
    } else if(selected) {
        const bool lead=knownSpeed&&speed>.5&&time>=selected->box.leadTime&&time-selected->box.leadTime<maxVisionAgeSeconds;
        d.prompt=selected->box.category==0?Prompt::pedestrianSpace:
            (selected->box.category==1||selected->box.category==3)?Prompt::riderSpace:lead?Prompt::vehicleSpace:Prompt::roadUserQuiet;
        d.reason=selected->box.category==0?"sustained person in forward attention zone":
            (selected->box.category==1||selected->box.category==3)?"sustained two-wheeler in forward attention zone":
            "sustained vehicle in forward attention zone";
        const bool entry=selected->entryPending&&!selected->spoken;
        if(entry)d.reason="sustained actor entering forward attention zone";
        const bool moreVulnerable=entry||((lastPrompt_==Prompt::vehicleSpace||lastPrompt_==Prompt::roadUserQuiet)&&
            (d.prompt==Prompt::pedestrianSpace||d.prompt==Prompt::riderSpace))||
            (lastPrompt_==Prompt::riderSpace&&d.prompt==Prompt::pedestrianSpace);
        if(lastPrompt_!=Prompt::camera&&time-lastSpoken_<(moreVulnerable?3.5:8)){
            d.prompt=Prompt::none;d.reason="learner attention cue spacing";return d;
        }
    } else {d.reason=sceneBits_?"watching relevant traffic; no new attention cue":"no sustained relevant road user";return d;}
    if(selected) {
        pendingActorId_=selected->id;
        pendingEvidenceKind_=kind;
        d.actorId=selected->id;d.actorCategory=selected->box.category;d.evidenceKind=kind;
        d.actorX=selected->box.x;d.actorY=selected->box.y;d.actorW=selected->box.w;d.actorH=selected->box.h;
        d.entryReferenceId=selected->entryReferenceId;
        d.newEncounter=newEncounterEligible(d.actorId,d.entryReferenceId,time);
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
