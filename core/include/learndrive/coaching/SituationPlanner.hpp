#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace ld::coaching {
struct Candidate;struct WorldState;class RiskCoach;class TaskCoach;
// One composed instruction: context clauses then actions, from recorded fragments.
struct Utterance {
    int id{},priority{};
    std::array<int,8> fragments{};
    size_t count{};
};
// The shared picture of the moment, turned into one instructor-style utterance.
//
// The existing pipeline still decides WHETHER to speak and about WHAT first (the
// primary instruction, after evidence validation, risk ranking and pacing). The
// planner then reasons over everything else true at the same moment: the slope
// (stopping on a downhill needs engine braking), the junction phase, every other
// assessed road user and other valid instructions. It plans a small, consistent
// action set (conflicts removed, most urgent first) and states the context that
// justifies it, e.g. "Uphill. Oncoming truck. Person on the left. Keep left.
// Ease off the accelerator. Give them space." Secondary instructions it absorbs
// are recorded so they are not repeated separately. Urgent moments stay short.
class SituationPlanner {
public:
    // Returns true and fills `out` (fragments, priority) and the primary's
    // secondary identities when composition adds context or another instruction.
    static bool compose(Candidate& primary,const WorldState& safe,const RiskCoach&,const TaskCoach&,Utterance& out) noexcept;
};
}
