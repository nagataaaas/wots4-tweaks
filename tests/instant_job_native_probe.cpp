// Installs mod.dll on a copy of the exe mapped into this process and calls the hooked mission
// judgement 0x50d410 with a synthetic mission manager, mission table and game state. The
// game's judgement needs the running game, so on the copy it is replaced, after the
// instructions MinHook moves, by a stub that either does nothing or reports a success the way
// the game does (+0xc = 0, +8 = 2). The game's own row lookup (0x50cb70) and accepting
// (0x50d020) run on the synthetic objects.
#include <cstdio>
#include <cstring>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t judgeMission = 0x50d410;
constexpr uintptr_t afterMovedInstructions = 0x50d416;  // push ebp; mov ebp,esp; and esp,-8
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uintptr_t eventStateGlobal = 0x9eb5f8;
constexpr uint32_t rowSize = 0x4c;
constexpr uint32_t acceptedMissionsOffset = 0x45f66;
constexpr uint32_t missionFlagsOffset = 0x134;
constexpr uint32_t missionStatesOffset = 0x33bc;  // int16 per ID; -1 = never accepted
constexpr uint32_t todayOffset = 0xbf0;
constexpr int16_t today = 17;

struct MissionRow {
    int16_t id;
    int16_t successCount;
    int8_t breakCount;
};

class SyntheticGame {
public:
    std::vector<uint8_t> gameState = std::vector<uint8_t>(0x46400);
    std::vector<uint8_t> manager = std::vector<uint8_t>(0x48);
    std::vector<uint8_t> eventState = std::vector<uint8_t>(0x1400);

    explicit SyntheticGame(std::initializer_list<MissionRow> rows) : rowBytes(rows.size() * rowSize) {
        uint8_t* row = rowBytes.data();
        for (const MissionRow& source : rows) {
            *reinterpret_cast<int16_t*>(row) = source.id;
            *reinterpret_cast<int16_t*>(row + 0x10) = source.successCount;
            row[0x13] = static_cast<uint8_t>(source.breakCount);
            row[0xe] = 3;  // Deadline in days
            row += rowSize;
        }
        table[0] = static_cast<uint32_t>(rows.size());
        table[1] = reinterpret_cast<uint32_t>(rowBytes.data());
        holder[0] = 3;  // MissionData.l loaded
        holder[2] = reinterpret_cast<uint32_t>(table);
        *reinterpret_cast<uint32_t**>(manager.data()) = holder;
        *reinterpret_cast<uint8_t**>(manager.data() + 4) = gameState.data();
        *reinterpret_cast<int16_t*>(gameState.data() + todayOffset) = today;
        for (int id = 0; id < 256; ++id) missionState(id) = -1;
    }
    const uint8_t* row(int16_t id) const {
        for (size_t offset = 0; offset < rowBytes.size(); offset += rowSize)
            if (*reinterpret_cast<const int16_t*>(rowBytes.data() + offset) == id) return rowBytes.data() + offset;
        return nullptr;
    }
    uint16_t& accepted(uint32_t index) {
        return *reinterpret_cast<uint16_t*>(gameState.data() + acceptedMissionsOffset + index * 0x10);
    }
    int16_t acceptedDay(uint32_t index) {
        return *reinterpret_cast<int16_t*>(gameState.data() + acceptedMissionsOffset + index * 0x10 + 4);
    }
    uint8_t& flag(uint32_t number) { return gameState[missionFlagsOffset + number]; }
    int16_t& missionState(int id) { return *reinterpret_cast<int16_t*>(gameState.data() + missionStatesOffset + id * 2); }
    const uint8_t*& currentRow() { return *reinterpret_cast<const uint8_t**>(manager.data() + 0xc); }
    int32_t& stage() { return *reinterpret_cast<int32_t*>(manager.data() + 8); }
    int8_t successVariable() const { return static_cast<int8_t>(manager[0x11]); }
    int8_t breakablesAtStart() const { return static_cast<int8_t>(manager[0x15]); }
    // What the game's success handling does to the manager and the accepted missions.
    void clearAccepted() {
        for (uint32_t index = 0; index < 50; ++index) accepted(index) = 0;
    }

private:
    std::vector<uint8_t> rowBytes;
    uint32_t table[2] = {};
    uint32_t holder[3] = {};
};

// mov esp,ebp; pop ebp; ret: the game's judgement finds nothing to do.
const uint8_t judgesNothing[] = {0x8b, 0xe5, 0x5d, 0xc3};
// mov dword [ecx+0xc],0; mov dword [ecx+8],2; mov esp,ebp; pop ebp; ret: a success.
const uint8_t judgesSuccess[] = {0xc7, 0x41, 0x0c, 0x00, 0x00, 0x00, 0x00, 0xc7, 0x41, 0x08, 0x02, 0x00, 0x00, 0x00,
    0x8b, 0xe5, 0x5d, 0xc3};

void replaceGameJudgement(const uint8_t* stub, size_t length) {
    std::memcpy(reinterpret_cast<void*>(va(afterMovedInstructions)), stub, length);
}

void judge(SyntheticGame& synthetic, const uint8_t* stub, size_t length) {
    replaceGameJudgement(stub, length);
    const uintptr_t function = va(judgeMission);
    uint8_t* manager = synthetic.manager.data();
    __asm {
        mov ecx, manager
        call function
    }
}

void judgeNothing(SyntheticGame& synthetic) { judge(synthetic, judgesNothing, sizeof(judgesNothing)); }
void judgeSuccess(SyntheticGame& synthetic) { judge(synthetic, judgesSuccess, sizeof(judgesSuccess)); }

template <typename T>
void setGlobal(uintptr_t global, T value) {
    *reinterpret_cast<T*>(va(global)) = value;
}

// Accepts the mission, runs one frame that starts it, and one that the game judges a success.
void completeOnce(SyntheticGame& synthetic, uint16_t mission) {
    synthetic.clearAccepted();
    synthetic.accepted(7) = mission;
    judgeSuccess(synthetic);
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_instant_job")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    const uint8_t breakCompare = codeByte(0x50d4be);  // cmp edx,eax of the breaking check
    codeByte(0x50d4be) = 0x39;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Instant Missions: the mission manager is not the known version at 0x50d410; the tweak is "
                     "skipped") &&
              loggedText("Tweak instant_job could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(judgeMission) && optionIndex(L"Instant Missions") == noItem && codeByte(judgeMission) == 0x55,
        "nothing is claimed, hooked or shown");
    codeByte(0x50d4be) = breakCompare;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(judgeMission, 5) && codeByte(judgeMission) == jumpOpcode, "0x50d410 is claimed (5 bytes) and hooked");
    const uint32_t item = optionIndex(L"Instant Missions");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"なりわい即完了",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    SyntheticGame synthetic({{3, 1, 0}, {5, 0, 4}, {171, 1, 0}, {179, 1, 0}, {202, 1, 0}, {203, 1, 0}});
    setGlobal(gameStateGlobal, synthetic.gameState.data());
    setGlobal(eventStateGlobal, static_cast<uint8_t*>(nullptr));
    synthetic.accepted(4) = 3;

    judgeNothing(synthetic);
    check(synthetic.currentRow() == nullptr, "off: an accepted mission waits for its area as in the game");

    changeOption(item, 1);
    judgeNothing(synthetic);
    check(synthetic.currentRow() == synthetic.row(3) && synthetic.stage() == 1 && synthetic.successVariable() == 1,
        "on: the accepted mission is put into the manager with its success variable met");
    synthetic.currentRow() = nullptr;

    synthetic.clearAccepted();
    synthetic.accepted(2) = 162;  // Not in the mission table
    synthetic.accepted(3) = 1;    // Outside the missions
    synthetic.accepted(9) = 5;
    judgeNothing(synthetic);
    check(synthetic.currentRow() == synthetic.row(5) && synthetic.successVariable() == 0 &&
              synthetic.breakablesAtStart() == 0x7f,
        "on: missions without a row are passed over; breaking starts with the most objects to have broken");
    synthetic.currentRow() = nullptr;

    synthetic.eventState[0x1327] = 1;
    setGlobal(eventStateGlobal, synthetic.eventState.data());
    judgeNothing(synthetic);
    check(synthetic.currentRow() == nullptr, "on: no mission starts while the game holds mission starts (0x1327)");
    setGlobal(eventStateGlobal, static_cast<uint8_t*>(nullptr));

    completeOnce(synthetic, 3);
    check(synthetic.currentRow() == nullptr && synthetic.stage() == 2 &&
              loggedText("Instant Missions: mission 3 was completed right away."),
        "on: a mission the game judges a success is logged once");

    synthetic.flag(4) = 10;
    completeOnce(synthetic, 171);
    check(synthetic.flag(4) == 20 && loggedText("mission 171 was completed right away and 1 story flag(s) were written"),
        "on: story mission 171 raises flag 4 to 20 as its target would");
    synthetic.flag(4) = 50;
    completeOnce(synthetic, 171);
    check(synthetic.flag(4) == 50, "on: a flag already past the value is kept");

    synthetic.flag(5) = 0;
    synthetic.flag(9) = 30;
    completeOnce(synthetic, 179);
    check(synthetic.flag(9) == 20 && synthetic.flag(5) == 20, "on: mission 179 sets flag 9 to 20 and raises flag 5");

    completeOnce(synthetic, 202);
    bool acceptedNext = false;
    for (uint32_t index = 0; index < 50; ++index)
        if (synthetic.accepted(index) == 203 && synthetic.acceptedDay(index) == today) acceptedNext = true;
    check(acceptedNext && synthetic.missionState(203) == 0 &&
              loggedText("mission 202 was completed right away and mission 203 was accepted to continue it"),
        "on: mission 202 is followed by accepting 203 through the game's own code");

    synthetic.clearAccepted();
    synthetic.accepted(0) = 3;
    synthetic.stage() = 1;
    synthetic.currentRow() = nullptr;
    judgeNothing(synthetic);
    synthetic.manager[0x11] = 0;
    changeOption(item, 0);
    judgeNothing(synthetic);
    check(synthetic.successVariable() == 0, "off again: a mission in the manager is left to the game");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"instant_job", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [instant_job] enabled=1");

    return finish("instant_job_native_probe");
}
