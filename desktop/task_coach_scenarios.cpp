// Reproducible candidate-stage episode timelines. Supplied camera/GPS clocks are
// synthetic. This does not test pixel perception, driving quality or road safety.
#include "learndrive/coaching/CoachingEngine.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
using namespace ld;
using namespace ld::coaching;
int main(){
    const char* names[]={"early_steady","early_slowing","late_arrival","location_loss"};
    for(int scenario=0;scenario<4;++scenario){
        auto e=std::make_unique<CoachingEngine>();e->configure(1,false,1);e->beginSource(1);e->enableJunctionLesson(true);
        TaskPhase last=TaskPhase::Off;int deliveries=0;
        for(int i=0;i<=360;++i){
            const double t=1+i*.1,seconds=(scenario==2?15:35)-(t-1);
            const double speed=scenario==1?std::max(12.,30-std::max(0.,t-2)*1.0):30;
            WorldState w;w.sourceId=1;w.frameSequence=i+1;w.now=w.captureTime=t;w.active=true;
            w.telemetry={t,speed,0,0,1,true,true,true,false,false,false};
            // A junction the app found on the map itself: steps by time (act-by). Google's turns go
            // by Google's distance and are covered by the drive-coach and C API tests.
            if(seconds>=0)w.route={17,9,2,t,seconds*speed/3.6,seconds,scenario==3&&t>=3?1:t,4,true,false};
            const auto d=e->decide(w,i+1);
            const int prompt=d.eligible?int(d.ticket.candidate.prompt):0;
            const bool accepted=prompt&&e->accept(i+1,prompt,w,t,false);if(accepted)++deliveries;
            const auto phase=e->task().progress().phase;
            if(phase!=last||accepted||i==360){
                std::cout<<"{\"scenario\":\""<<names[scenario]<<"\",\"evidence\":\"synthetic_candidate_stage\",\"time\":"<<t
                    <<",\"supplied_lead_seconds\":"<<seconds<<",\"selected_prompt\":"<<prompt<<",\"visual_accepted\":"<<(accepted?"true":"false")
                    <<",\"instructor_labels\":null,\"task\":"<<e->task().json()<<"}\n";
            }
            last=phase;
        }
        // Act-by: steady 30 km/h gets preparation + "brake, change down"; slowing below 25 km/h
        // gets preparation + junction entry (no separate approach step). A junction first
        // known 15 s ahead is still coached in time; location lost before the preparation
        // moment is never spoken about.
        const int expected=scenario==0?2:scenario==1?2:scenario==2?2:0;
        if(deliveries!=expected){std::cerr<<names[scenario]<<" expected "<<expected<<" deliveries; got "<<deliveries<<'\n';return 1;}
    }
}
