// What the targets' action scripts write when a story mission (171 to 212) succeeds, so that
// the next request of its client appears. Completing a mission from the mission manager
// skips those scripts, so the tweak writes the same values. Missions 177 and 187 to 194 are
// left out: the game's own success handling (0x50d980) writes their flags.
#pragma once
#include <cstdint>

namespace tweaks::instant_job {

// Mission flags are bytes at game state +0x134 + number (save 0x011C + number); the scripts
// write them with command 117 (0x41776f: mov [ebx+edx+0x134],cl).
constexpr uint32_t missionFlagsOffset = 0x134;

struct FlagWrite {
    uint8_t flag;
    uint8_t value;
    bool onlyRaise;  // The scripts skip the write when the flag already holds this value or more
};

struct FollowUp {
    int16_t mission;
    FlagWrite writes[2];
    uint8_t writeCount;
    // The next part of a two-part mission. 202 and 205 end in a conversation that drops them
    // and accepts 203 or 206, and never set the success variable.
    int16_t nextMission;
};

constexpr int16_t noNextMission = 0;
constexpr uint8_t policeFlag = 4, gangsterFlag = 5, homelessFlag = 7, gangsterMetFlag = 9;
constexpr uint8_t finaleFlag = 25, finaleEndFlag = 26;
constexpr uint8_t shirubeKondoFlag = 28, shirubeHijikataFlag = 29, shirubeDonaFlag = 30, shirubeSayoFlag = 31;

constexpr FlagWrite raiseFlag(uint8_t flag, uint8_t value) { return {flag, value, true}; }
constexpr FlagWrite setFlag(uint8_t flag, uint8_t value) { return {flag, value, false}; }

// From the success branches of the targets' scripts (O_*, G_*, K_*, G4_*, Target_Last, Kondo,
// Hijikata, Dona, Sayo).
constexpr FollowUp followUps[] = {
    {171, {raiseFlag(policeFlag, 20)}, 1, noNextMission},
    {172, {raiseFlag(policeFlag, 30)}, 1, noNextMission},
    {173, {raiseFlag(policeFlag, 40)}, 1, noNextMission},
    {174, {raiseFlag(policeFlag, 50)}, 1, noNextMission},
    {175, {raiseFlag(policeFlag, 60)}, 1, noNextMission},
    {176, {raiseFlag(policeFlag, 70)}, 1, noNextMission},
    {178, {raiseFlag(policeFlag, 90)}, 1, noNextMission},
    {179, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 20)}, 2, noNextMission},
    {180, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 30)}, 2, noNextMission},
    {181, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 40)}, 2, noNextMission},
    {182, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 50)}, 2, noNextMission},
    {183, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 60)}, 2, noNextMission},
    {184, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 70)}, 2, noNextMission},
    {185, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 80)}, 2, noNextMission},
    {186, {setFlag(gangsterMetFlag, 20), raiseFlag(gangsterFlag, 90)}, 2, noNextMission},
    {195, {raiseFlag(homelessFlag, 20)}, 1, noNextMission},
    {196, {raiseFlag(homelessFlag, 30)}, 1, noNextMission},
    {197, {raiseFlag(homelessFlag, 40)}, 1, noNextMission},
    {199, {raiseFlag(homelessFlag, 50)}, 1, noNextMission},
    {201, {raiseFlag(homelessFlag, 60)}, 1, noNextMission},
    {202, {}, 0, 203},
    {203, {raiseFlag(homelessFlag, 70)}, 1, noNextMission},
    {204, {raiseFlag(homelessFlag, 80)}, 1, noNextMission},
    {205, {}, 0, 206},
    {206, {raiseFlag(homelessFlag, 90)}, 1, noNextMission},
    {207, {setFlag(finaleFlag, 50)}, 1, noNextMission},
    {208, {setFlag(finaleFlag, 100), setFlag(finaleEndFlag, 100)}, 2, noNextMission},
    {209, {setFlag(shirubeKondoFlag, 1)}, 1, noNextMission},
    {210, {setFlag(shirubeHijikataFlag, 1)}, 1, noNextMission},
    {211, {setFlag(shirubeDonaFlag, 1)}, 1, noNextMission},
    {212, {setFlag(shirubeSayoFlag, 1)}, 1, noNextMission},
};

inline const FollowUp* findFollowUp(int16_t mission) {
    for (const FollowUp& followUp : followUps)
        if (followUp.mission == mission) return &followUp;
    return nullptr;
}

// Writes the flags of the follow-up to the game state, as the scripts would.
inline void writeFlags(const FollowUp& followUp, uint8_t* gameState) {
    uint8_t* flags = gameState + missionFlagsOffset;
    for (uint8_t index = 0; index < followUp.writeCount; ++index) {
        const FlagWrite& write = followUp.writes[index];
        if (write.onlyRaise && flags[write.flag] >= write.value) continue;
        flags[write.flag] = write.value;
    }
}

}  // namespace tweaks::instant_job
