#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace ld::coaching {
// What the learner was MEASURED doing about a road user the coach had something to say about:
// their speed fell before the coach spoke (or needed to), after it spoke, or not at all.
// Speed only: the app cannot see mirrors, eyes, steering or pedals, so this is an
// observation of slowing, never a grade of driving.
enum class LearnerOutcome : int { Before=1, After=2, NoResponse=3 };
// "You first" (0.8.5). A real instructor says less as the learner needs less. Per kind of
// road-user situation, the last 30 measured outcomes across drives decide how the coach speaks:
//   level 0  in full (every new learner, and whenever the record is thin or mixed);
//   level 1  (slows when told) once their measured speed shows the slowing is already done, the
//            instruction comes without its slowing part: "Oncoming bus. Keep left.";
//   level 2  (often slows before being told) once the slowing is done, nothing is said and the
//            screen shows that the coach has seen the road user.
// A learner who has not slowed is always told in full, at the same moment as before: nothing is
// delayed. Urgent instructions ("Brake now", crossing road users) are never shortened or left
// unsaid. Kept on the phone only.
class LearnerRecord {
public:
    static constexpr int skills=4;
    static constexpr size_t window=30;
    struct Tally {int before{},after{},none{};int total() const noexcept {return before+after+none;}};
    // 0 people ahead, 1 two-wheelers ahead, 2 vehicles ahead, 3 oncoming traffic.
    static const char* skillName(int skill) noexcept;
    void enableWaiting(bool enabled) noexcept {waiting_=enabled;}
    bool waiting() const noexcept {return waiting_;}
    // Only the learner's own driving counts: the road camera with their measured speed. A test
    // video or a simulation (a manual or scripted speed) neither adds to the record nor uses it.
    void measuredDriving(bool value) noexcept {measured_=value;}
    bool measuredDriving() const noexcept {return measured_;}
    // A new drive: this-drive tallies restart; outcomes from it count as one more drive. The
    // levels the coach applies are fixed here for the whole drive.
    void newDrive() noexcept;
    void note(int skill,LearnerOutcome outcome) noexcept;
    // What the coach applies: the level the record had earned when this drive began (or when it
    // was restored). This drive's outcomes count from the next drive, so the coach never changes
    // how it speaks in the middle of one. 0 when switched off or not the learner's own driving.
    int waitLevel(int skill) const noexcept;
    // What the record has earned as of now (0 when switched off): for display between drives.
    int earnedLevel(int skill) const noexcept;
    Tally recent(int skill) const noexcept;
    Tally drive(int skill) const noexcept;
    int drives(int skill) const noexcept;
    // For storage between drives: out[0] = drives with an outcome, then outcomes oldest first.
    size_t outcomes(int skill,int* out,size_t capacity) const noexcept;
    bool restore(int skill,int drives,const int* outcomes,size_t count) noexcept;
    void forget() noexcept;
    std::string json() const;
private:
    struct Skill {std::array<uint8_t,window> ring{};size_t count{},cursor{};int drives{},level{};bool thisDrive{};Tally drive{};};
    int earned(int skill) const noexcept;
    std::array<Skill,skills> skills_{};
    bool waiting_=true,measured_=true;
};
}
