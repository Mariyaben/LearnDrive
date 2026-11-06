#pragma once
#include "learndrive/coaching/CoachingEngine.hpp"
#include <cmath>
namespace ld::coaching {
struct BoundedModelProposal {
    Mode mode=Mode::SILENCE;
    Prompt prompt=Prompt::none;
    Identity identity{};
    uint64_t source{},revision{},generation{};
    double createdAt{},expiresAt{};
    bool valid{};
};
// Optional routine-usefulness selector. Never installs itself as live authority.
// The engine still independently validates every current candidate/ticket.
class HybridPolicy final : public DecisionPolicy {
public:
    bool propose(const BoundedModelProposal& p) noexcept {
        if(!p.valid||int(p.mode)<0||int(p.mode)>3||!p.source||!p.revision||
           !std::isfinite(p.createdAt)||!std::isfinite(p.expiresAt)||p.expiresAt<=p.createdAt||p.expiresAt-p.createdAt>2)return false;
        proposal_=p;return true;
    }
    void cancel() noexcept {proposal_={};}
    PolicyChoice choose(const PolicyContext& c) const noexcept override {
        const auto fallback=temporal_.choose(c);
        if(fallback.mode==Mode::WARN)return fallback;
        const auto& p=proposal_;
        if(!p.valid||!c.scene||p.source!=c.world.sourceId||p.revision!=c.scene->revision||p.generation!=c.scene->generation||
           c.world.now<p.createdAt||c.world.now>=p.expiresAt)return fallback;
        if(p.mode==Mode::SILENCE)return {Mode::SILENCE,0,"MODEL_SILENCE"};
        if(p.mode==Mode::DEFER)return c.world.candidateCount?PolicyChoice{Mode::DEFER,0,"MODEL_DEFER"}:fallback;
        for(size_t i=0;i<c.world.candidateCount;++i)if(c.world.candidates[i].identity==p.identity&&c.world.candidates[i].prompt==p.prompt) {
            if(c.world.candidates[i].urgent!=(p.mode==Mode::WARN))return fallback;
            return {p.mode,i,"MODEL_SELECTED_APPROVED_EVENT"};
        }
        return fallback;
    }
private:
    TemporalPolicy temporal_;
    BoundedModelProposal proposal_{};
};
}
