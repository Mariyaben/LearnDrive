#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace ld::coaching {
struct Candidate;struct WorldState;struct PolicyContext;struct PolicyChoice;struct Ticket;class TaskCoach;
enum class DriveTask { Observing, Junction, Bend, Grade, Space, Vehicle };
// Coordinates the supported tasks within one drive. It never invents a sensor
// measurement, promotes a routine cue to a warning, or infers driving mastery.
class DriveCoach {
public:
    void enable(bool enabled) noexcept;
    bool enabled() const noexcept {return enabled_;}
    void sourceChanged() noexcept;
    void observe(const WorldState&,const TaskCoach&,const Ticket&) noexcept;
    PolicyChoice choose(const PolicyContext&) const noexcept;
    bool valid(const Candidate&,const WorldState&,double now,bool started=false) const noexcept;
    void selected(const Candidate&,double now) noexcept;
    void delivered(const Candidate&,double now) noexcept;
    std::string json(const TaskCoach&) const;
    // A measured, developing closing conflict for the candidate's own actor.
    static bool developingConflict(const Candidate&,const WorldState&) noexcept;
private:
    bool enabled_{};
    DriveTask task_=DriveTask::Observing;
    const char* phase_="OFF";
    double lastDelivery_=-1;
    std::array<uint64_t,6> deliveries_{};
};
}
