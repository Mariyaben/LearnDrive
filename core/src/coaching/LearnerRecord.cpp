#include "learndrive/coaching/LearnerRecord.hpp"
#include <algorithm>
#include <sstream>
namespace ld::coaching {
const char* LearnerRecord::skillName(int skill) noexcept {
    switch(skill){case 0:return "people ahead";case 1:return "two-wheelers ahead";case 2:return "vehicles ahead";case 3:return "oncoming traffic";default:return "unknown";}
}
void LearnerRecord::newDrive() noexcept {for(int k=0;k<skills;++k){auto& s=skills_[size_t(k)];s.level=earned(k);s.thisDrive=false;s.drive={};}}
void LearnerRecord::note(int skill,LearnerOutcome outcome) noexcept {
    if(!measured_||skill<0||skill>=skills||int(outcome)<1||int(outcome)>3)return;
    auto& s=skills_[size_t(skill)];
    s.ring[s.cursor]=uint8_t(outcome);s.cursor=(s.cursor+1)%window;s.count=std::min(s.count+1,window);
    if(!s.thisDrive){s.thisDrive=true;if(s.drives<1000000)++s.drives;}
    if(outcome==LearnerOutcome::Before)++s.drive.before;else if(outcome==LearnerOutcome::After)++s.drive.after;else ++s.drive.none;
}
LearnerRecord::Tally LearnerRecord::recent(int skill) const noexcept {
    Tally t;if(skill<0||skill>=skills)return t;
    const auto& s=skills_[size_t(skill)];
    for(size_t i=0;i<s.count;++i){const int o=s.ring[i];if(o==1)++t.before;else if(o==2)++t.after;else if(o==3)++t.none;}
    return t;
}
LearnerRecord::Tally LearnerRecord::drive(int skill) const noexcept {return skill<0||skill>=skills?Tally{}:skills_[size_t(skill)].drive;}
int LearnerRecord::drives(int skill) const noexcept {return skill<0||skill>=skills?0:skills_[size_t(skill)].drives;}
int LearnerRecord::waitLevel(int skill) const noexcept {
    return waiting_&&measured_&&skill>=0&&skill<skills?skills_[size_t(skill)].level:0;
}
int LearnerRecord::earnedLevel(int skill) const noexcept {return waiting_?earned(skill):0;}
int LearnerRecord::earned(int skill) const noexcept {
    if(skill<0||skill>=skills)return 0;
    const Tally t=recent(skill);const int n=t.total(),responded=t.before+t.after,d=skills_[size_t(skill)].drives;
    // Never on thin evidence or within the first drives: a new learner is told at once, in full.
    if(n>=24&&d>=4&&responded*5>=n*4&&t.before>=8)return 2;
    if(n>=12&&d>=2&&responded*10>=n*7)return 1;
    return 0;
}
size_t LearnerRecord::outcomes(int skill,int* out,size_t capacity) const noexcept {
    if(skill<0||skill>=skills||!out||capacity<1)return 0;
    const auto& s=skills_[size_t(skill)];
    size_t n=0;out[n++]=s.drives;
    const size_t first=s.count<window?0:s.cursor;
    for(size_t i=0;i<s.count&&n<capacity;++i)out[n++]=s.ring[(first+i)%window];
    return n;
}
bool LearnerRecord::restore(int skill,int drives,const int* outcomes,size_t count) noexcept {
    if(skill<0||skill>=skills||drives<0||drives>1000000||(count&&!outcomes)||count>window)return false;
    for(size_t i=0;i<count;++i)if(outcomes[i]<1||outcomes[i]>3)return false;
    auto& s=skills_[size_t(skill)];s={};
    for(size_t i=0;i<count;++i)s.ring[i]=uint8_t(outcomes[i]);
    s.count=count;s.cursor=count%window;s.drives=drives;s.level=earned(skill);
    return true;
}
void LearnerRecord::forget() noexcept {skills_={};}
std::string LearnerRecord::json() const {
    std::ostringstream o;o<<"{\"waiting\":"<<(waiting_?"true":"false")<<",\"skills\":[";
    Tally all;
    for(int k=0;k<skills;++k){const Tally r=recent(k),d=drive(k);
        all.before+=d.before;all.after+=d.after;all.none+=d.none;
        o<<(k?",":"")<<"{\"skill\":\""<<skillName(k)<<"\",\"wait_level\":"<<waitLevel(k)<<",\"earned_level\":"<<earnedLevel(k)<<",\"drives\":"<<drives(k)
         <<",\"recent\":{\"before\":"<<r.before<<",\"after\":"<<r.after<<",\"none\":"<<r.none<<"}"
         <<",\"drive\":{\"before\":"<<d.before<<",\"after\":"<<d.after<<",\"none\":"<<d.none<<"}}";}
    o<<"],\"drive\":{\"before\":"<<all.before<<",\"after\":"<<all.after<<",\"none\":"<<all.none<<"}}";
    return o.str();
}
}
