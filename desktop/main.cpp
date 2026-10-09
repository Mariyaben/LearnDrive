#include "learndrive/decision.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
int main(int argc,char** argv) {
    if(argc!=2) {std::cerr<<"Usage: learndrive_desk <scenario.csv>\n";return 2;}
    std::ifstream input(argv[1]);if(!input){std::cerr<<"Cannot open scenario\n";return 2;}
    ld::DecisionEngine engine;ld::DrivingCoach coach;std::string line;double previous=-1;int alerts=0;
    std::getline(input,line);
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line!="time,speed_kmh,pitch_deg,braking,tti_s,confidence,in_path,allow_gear") {std::cerr<<"Unexpected CSV header\n";return 2;}
    try {
        while(std::getline(input,line)) {
            if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
            std::stringstream row(line);std::string field;std::vector<double> v;
            while(std::getline(row,field,',')){size_t used=0;double value=std::stod(field,&used);if(used!=field.size())throw std::runtime_error("invalid field");v.push_back(value);}
            if(v.size()!=8||!std::isfinite(v[0])||v[0]<=previous)throw std::runtime_error("eight fields and increasing timestamps required");
            previous=v[0];ld::Telemetry t;t.time=v[0];t.speedKmh=v[1];t.pitchDegrees=v[2];t.braking=v[3]!=0;t.calibrated=t.speedValid=t.pitchValid=true;
            auto d=engine.evaluate(t,{v[0],v[4],v[5],v[6]!=0},true,v[7]!=0);
            auto guidance=coach.evaluate(t,true,v[7]!=0);
            if(ld::priority(guidance.prompt)>ld::priority(d.prompt))d=guidance;
            std::cout<<v[0]<<"s: "<<d.reason;
            if(d.prompt!=ld::Prompt::none) {std::cout<<" -> "<<ld::promptText(d.prompt);coach.accepted(d.prompt,v[0]);if(d.prompt==ld::Prompt::ease||d.prompt==ld::Prompt::brake||d.prompt==ld::Prompt::emergency)engine.accepted(v[0]);++alerts;}
            std::cout<<'\n';
        }
    } catch(const std::exception& e) {std::cerr<<"Scenario error: "<<e.what()<<'\n';return 2;}
    std::cout<<alerts<<" prompts\n";return 0;
}
