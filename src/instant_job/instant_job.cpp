// The mission manager [0x9e96c0] (MissionManager.cpp) carries one mission at a time and judges
// it every frame in 0x50d410. A mission normally gets into the manager only when the player
// enters its area, and succeeds when its target's script sets the success variable M1 (or
// enough objects are broken). This tweak hooks the judgement: it puts an accepted mission into
// the manager, meets its success condition, and lets the game's own success handling pay the
// reward and keep the records. Story missions also get the flags their targets would write.
#include "instant_job.h"
#include <cstdio>
#include <cstring>
#include "../game_code.h"
#include "mission_follow_ups.h"

namespace tweaks {

namespace {

using namespace instant_job;

constexpr uintptr_t judgeMission = 0x50d410;     // thiscall, ecx = the mission manager
constexpr uint32_t judgeMissionHookLength = 5;   // push ebp; mov ebp,esp; and esp,-8
constexpr uintptr_t findMissionRow = 0x50cb70;   // esi = the mission table, push ID; ret 4
constexpr uintptr_t acceptMission = 0x50d020;    // ebx = ID, push the manager; ret 4 (command 122)
constexpr uintptr_t missionManagerGlobal = 0x9e96c0;
constexpr uintptr_t eventStateGlobal = 0x9eb5f8;
// 0x50d170 starts no mission while this byte of [0x9eb5f8] is set (0x50d19a); its meaning is unknown.
constexpr uint32_t noMissionStartOffset = 0x1327;

// The mission manager
constexpr uint32_t tableHolderOffset = 0x0;  // -> int32 3 once MissionData.l is loaded, +8 the table
constexpr int32_t tableLoaded = 3;
constexpr uint32_t loadedTableOffset = 0x8;
constexpr uint32_t gameStateOffset = 0x4;
constexpr uint32_t stageOffset = 0x8;  // int32
constexpr int32_t stageInProgress = 1;
constexpr int32_t stageSucceeded = 2;
constexpr uint32_t currentRowOffset = 0xc;  // The mission's row in the table, or null
constexpr uint32_t variablesOffset = 0x10;  // int8 M0 to M3 (script variables 30 to 33)
constexpr uint32_t successVariableOffset = 0x11;  // M1: >= row +0x10 succeeds, < 0 fails
constexpr uint32_t clearedByteOffset = 0x14;
constexpr uint32_t breakablesAtStartOffset = 0x15;  // int8; broken = this - the objects left
constexpr uint32_t targetsOffset = 0x18;  // 8 characters and their count, up to +0x3b
constexpr uint32_t targetsSize = 0x24;

// A row of MissionData.l (0x4c bytes)
constexpr uint32_t successCountOffset = 0x10;  // int16, what M1 has to reach; 0 for breaking
constexpr uint32_t breakCountOffset = 0x13;    // int8, objects to break
constexpr int8_t mostBreakablesAtStart = 0x7f;

// Accepted missions in the game state: 50 entries of 16 bytes, the ID (uint16, 0 = free) first.
constexpr uint32_t acceptedMissionsOffset = 0x45f66;
constexpr uint32_t acceptedMissionCount = 50;
constexpr uint32_t acceptedMissionSize = 0x10;
constexpr int16_t firstMission = 3;  // 1 and 2 are not in MissionData.l
constexpr int16_t lastMission = 212;

using JudgeMission = void(__fastcall*)(uint8_t* manager);

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Instant Missions");
JudgeMission originalJudgeMission;

template <typename T>
T& field(uint8_t* object, uint32_t offset) {
    return *reinterpret_cast<T*>(object + offset);
}
template <typename T>
T field(const uint8_t* object, uint32_t offset) {
    return *reinterpret_cast<const T*>(object + offset);
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t judgePrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x81, 0xec, 0x0c, 0x01, 0x00, 0x00};
    const uint8_t judgeCaller[] = {0x8b, 0xce};  // mov ecx,esi; call 0x50d410
    // M1 against row +0x10: a negative M1 fails, M1 >= the count succeeds
    const uint8_t successVariable[] = {0x8a, 0x43, 0x11, 0x8b, 0x4b, 0x0c, 0x0f, 0xbf, 0x49, 0x10, 0xc6, 0x44, 0x24,
        0x0f, 0x00, 0xc6, 0x44, 0x24, 0x0e, 0x00, 0x84, 0xc0, 0x79, 0x07, 0xc6, 0x44, 0x24, 0x0e, 0x01, 0xeb, 0x10,
        0x85, 0xc9, 0x7e, 0x0c, 0x0f, 0xbe, 0xd0, 0x3b, 0xd1, 0x7c, 0x05, 0xc6, 0x44, 0x24, 0x0f, 0x01};
    // Breaking: +0x15 - the objects left >= row +0x13 succeeds
    const uint8_t breaking[] = {0x8b, 0x7b, 0x0c, 0x8a, 0x4f, 0x13, 0x84, 0xc9, 0x7e, 0x12, 0x0f, 0xbe, 0x53, 0x15,
        0x2b, 0xd0, 0x0f, 0xbe, 0xc1, 0x3b, 0xd0, 0x7c, 0x05, 0xc6, 0x44, 0x24, 0x0f, 0x01};
    // How 0x50d170 starts a mission: +0x10 = 0, +0x14 = 0, ..., +8 = 1, +0xc = the row
    const uint8_t clearVariables[] = {0x33, 0xc0, 0x89, 0x46, 0x10, 0x88, 0x46, 0x14};
    const uint8_t setStage[] = {0xc7, 0x46, 0x08, 0x01, 0x00, 0x00, 0x00, 0x89, 0x5e, 0x0c};
    // test eax,eax; je; cmp byte [eax+0x1327],0 after mov eax,[0x9eb5f8]
    const uint8_t noStartCheck[] = {0x85, 0xc0, 0x74, 0x0d, 0x80, 0xb8, 0x27, 0x13, 0x00, 0x00, 0x00};
    // Accepting: push ebp; mov ebp,esp; push esi; push edi; mov edi,[ebp+8]; mov esi,ebx;
    // then lea ecx,[edx+0x45f66]; cmp word [ecx],0 (the accepted missions)
    const uint8_t acceptPrologue[] = {0x55, 0x8b, 0xec, 0x56, 0x57, 0x8b, 0x7d, 0x08, 0x8b, 0xf3};
    const uint8_t acceptedMissions[] = {0x8d, 0x8a, 0x66, 0x5f, 0x04, 0x00, 0x66, 0x83, 0x39, 0x00};
    // mov eax,[ebp+8]; mov eax,[eax]; cmp dword [eax],3; jne; mov esi,[eax+8]; push ebx; call 0x50cb70
    const uint8_t findRow[] = {0x8b, 0x45, 0x08, 0x8b, 0x00, 0x83, 0x38, 0x03, 0x75, 0x44, 0x8b, 0x70, 0x08, 0x53};
    const uint8_t loadManager[] = {0x8b, 0x35};  // mov esi,[0x9e96c0]
    const uint8_t loadEventState = 0xa1;         // mov eax,[0x9eb5f8]
    return game.codeIs(judgeMission, judgePrologue, sizeof(judgePrologue)) &&
           game.codeIs(0x50cf6b, judgeCaller, sizeof(judgeCaller)) && game.callsTo(0x50cf6d, judgeMission) &&
           game.codeIs(0x50d469, successVariable, sizeof(successVariable)) &&
           game.codeIs(0x50d4ab, breaking, sizeof(breaking)) &&
           game.codeIs(0x50d27b, clearVariables, sizeof(clearVariables)) &&
           game.codeIs(0x50d29a, setStage, sizeof(setStage)) && game.codeIs(0x50d191, &loadEventState, 1) &&
           game.operandIs(0x50d192, eventStateGlobal) && game.codeIs(0x50d196, noStartCheck, sizeof(noStartCheck)) &&
           game.codeIs(acceptMission, acceptPrologue, sizeof(acceptPrologue)) &&
           game.codeIs(0x50d042, acceptedMissions, sizeof(acceptedMissions)) &&
           game.codeIs(0x50d06d, findRow, sizeof(findRow)) && game.callsTo(0x50d07b, findMissionRow) &&
           game.codeIs(0x4c327d, loadManager, sizeof(loadManager)) && game.operandIs(0x4c327f, missionManagerGlobal);
}

const uint8_t* findRowWithGameCode(const uint8_t* table, int32_t mission) {
    const uintptr_t function = game.address(findMissionRow);
    const uint8_t* row;
    __asm {
        push esi
        mov esi, table
        push mission
        call function
        mov row, eax
        pop esi
    }
    return row;
}

void acceptWithGameCode(uint8_t* manager, int32_t mission) {
    const uintptr_t function = game.address(acceptMission);
    __asm {
        push ebx
        mov ebx, mission
        push manager
        call function
        pop ebx
    }
}

const uint8_t* missionTable(const uint8_t* manager) {
    const auto* holder = field<const uint8_t*>(manager, tableHolderOffset);
    if (!holder || field<int32_t>(holder, 0) != tableLoaded) return nullptr;
    return field<const uint8_t*>(holder, loadedTableOffset);
}

bool missionStartsAreHeld() {
    const auto* eventState = reinterpret_cast<const uint8_t*>(game.readUint32(eventStateGlobal));
    return eventState && field<uint8_t>(eventState, noMissionStartOffset) != 0;
}

// The first accepted mission that has a row, or null. Runs every frame while the manager is
// idle: at most 50 reads, and a table lookup only for an accepted mission.
const uint8_t* findAcceptedMissionRow(const uint8_t* manager) {
    const uint8_t* table = missionTable(manager);
    const auto* gameState = field<const uint8_t*>(manager, gameStateOffset);
    if (!table || !gameState) return nullptr;
    const uint8_t* entry = gameState + acceptedMissionsOffset;
    for (uint32_t index = 0; index < acceptedMissionCount; ++index, entry += acceptedMissionSize) {
        const auto mission = field<int16_t>(entry, 0);
        if (mission < firstMission || mission > lastMission) continue;
        if (const uint8_t* row = findRowWithGameCode(table, mission)) return row;
    }
    return nullptr;
}

// Puts the mission into the manager as 0x50d170 does, without spawning its targets or showing its name.
void startMission(uint8_t* manager, const uint8_t* row) {
    std::memset(manager + targetsOffset, 0, targetsSize);
    field<int32_t>(manager, variablesOffset) = 0;
    field<uint8_t>(manager, clearedByteOffset) = 0;
    field<int32_t>(manager, stageOffset) = stageInProgress;
    field<const uint8_t*>(manager, currentRowOffset) = row;
}

void meetSuccessCondition(uint8_t* manager, const uint8_t* row) {
    const auto successCount = field<int16_t>(row, successCountOffset);
    if (successCount > 0) {
        field<int8_t>(manager, successVariableOffset) = static_cast<int8_t>(successCount);
        return;
    }
    if (field<int8_t>(row, breakCountOffset) > 0) field<int8_t>(manager, breakablesAtStartOffset) = mostBreakablesAtStart;
}

bool succeeded(const uint8_t* manager) {
    return field<const uint8_t*>(manager, currentRowOffset) == nullptr &&
           field<int32_t>(manager, stageOffset) == stageSucceeded;
}

// Runs once per completed mission, so it may log.
void finishStory(uint8_t* manager, int16_t mission) {
    const FollowUp* followUp = findFollowUp(mission);
    if (!followUp) {
        char line[96];
        sprintf_s(line, "mission %d was completed right away", mission);
        logger.write("INFO", line);
        return;
    }
    writeFlags(*followUp, field<uint8_t*>(manager, gameStateOffset));
    if (followUp->nextMission != noNextMission) acceptWithGameCode(manager, followUp->nextMission);
    char line[160];
    if (followUp->nextMission != noNextMission)
        sprintf_s(line, "mission %d was completed right away and mission %d was accepted to continue it", mission,
            followUp->nextMission);
    else
        sprintf_s(line, "mission %d was completed right away and %u story flag(s) were written as its target would",
            mission, followUp->writeCount);
    logger.write("INFO", line);
}

void __fastcall judgeMissionHook(uint8_t* manager) {
    if (enabled != toggleOn || !manager) {
        originalJudgeMission(manager);
        return;
    }
    if (!field<const uint8_t*>(manager, currentRowOffset) && !missionStartsAreHeld()) {
        if (const uint8_t* row = findAcceptedMissionRow(manager)) startMission(manager, row);
    }
    const uint8_t* row = field<const uint8_t*>(manager, currentRowOffset);
    if (!row) {
        originalJudgeMission(manager);
        return;
    }
    const auto mission = field<int16_t>(row, 0);
    meetSuccessCondition(manager, row);
    originalJudgeMission(manager);
    if (succeeded(manager)) finishStory(manager, mission);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the mission manager is not the known version", judgeMission);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, judgeMission, judgeMissionHookLength,
            reinterpret_cast<void*>(&judgeMissionHook), reinterpret_cast<void**>(&originalJudgeMission))) {
        logger.installFailed(error, judgeMission);
        return false;
    }
    return true;
}

// なりわい is "Missions" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"なりわい即完了", L"Instant Missions"}, &enabled, false),
};

}  // namespace

const Tweak instantJobTweak{L"instant_job", options, 1, install};

}  // namespace tweaks
