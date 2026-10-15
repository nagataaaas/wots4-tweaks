// Respawning enemies (AcAround.cpp, [[0x9e9640]+8]) are characters made beforehand and brought back
// again and again. A mission asks for 8 of them when its area loads (0x50d170, the row's +0x2c) and
// gives them on a later frame (0x50ce30 -> 0x436370). A defeated one comes back only once its body is
// off the screen, so bodies in view leave few to bring back. The tweak asks for more of the same
// enemies when a mission gives its 8, and gives each to the respawning enemies once it is made:
// making a character (0x43a990) only queues it, and AcManager builds it over the next frames.
//
// Missions only: a battle script (SetWakiZako) also lists its enemies in the battle's participant
// table, which keeps 32 (0x56f6d2) and silently drops the rest.
#include "enemy_pool_size.h"
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include "../game_code.h"

#pragma intrinsic(_ReturnAddress)

namespace tweaks {

namespace {

constexpr uintptr_t setUpRespawn = 0x436370;          // stdcall (respawn, ids, count, kills, deferred)
constexpr uint32_t setUpRespawnClaim = 6;             // push ebp; mov ebp,esp; sub esp,0x10
constexpr uintptr_t missionSetUpCall = 0x50cf16;      // in the mission's per-frame update 0x50ce30
constexpr uintptr_t missionSetUpReturn = 0x50cf1b;
constexpr uintptr_t requestCharacterCode = 0x43a990;  // ecx = definition, ebx = position; ret 0xc
constexpr uintptr_t putBackCode = 0x437150;           // eax = entry; push respawn; ret 4 (hides it)
constexpr uintptr_t growAndAppendCode = 0x437750;     // esi = list, eax = end; push entry; ret 4
constexpr uintptr_t managersGlobal = 0x9e9640;        // [ ]+8 is the respawning enemies
constexpr uintptr_t characterManagerGlobal = 0x9e9658;
constexpr uintptr_t missionManagerGlobal = 0x9e96c0;
constexpr uintptr_t zeroPosition = 0x9df27c;          // The position a mission makes its enemies at
// The wait 0x436370 gives each enemy, which Defeated Enemy Cleanup may change.
constexpr uintptr_t firstWaitOperand = 0x43646d;

constexpr uint32_t respawnOffset = 0x8;
constexpr uint32_t respawnListOffset = 0x14;          // m_ZakoList
constexpr uint32_t respawnStateOffset = 0x3c;
constexpr uint32_t respawnRunning = 1;
constexpr uint32_t missionRowOffset = 0xc;
constexpr uint32_t rowEnemyDefinitions = 0x2c;        // 8 int16, -1 for none
constexpr uint32_t rowEnemyCount = 8;
constexpr int16_t noDefinition = -1;
constexpr uint32_t characterParameters = 0xc;
constexpr uint32_t characterState = 0x10;
constexpr uint32_t characterReady = 3;                // As AcManager waits for (0x43a14e)
constexpr uint32_t parameterFlags = 0x2fc;
constexpr uint32_t flagRespawning = 0x10;             // Set by 0x436370, cleared by 0x436500

// Enemies are usually made within a few frames; after 10 seconds the rest are left alone.
constexpr uint32_t maxWaitFrames = 300;

enum PoolChoice : uint32_t { gameDefault, sixteen, twentyFour };
constexpr int32_t poolSizes[] = {0, 16, 24};
constexpr uint32_t maxExtraEnemies = 24;

struct RespawnEntry {
    uintptr_t character;
    int32_t waitFrames;
    uint8_t out;  // 0 while it waits to be brought back
    uint8_t padding[3];
};
static_assert(sizeof(RespawnEntry) == 12, "an entry of m_ZakoList is 12 bytes");

struct RespawnList {
    RespawnEntry* begin;
    RespawnEntry* end;
    RespawnEntry* capacityEnd;
};

// The tree node of the character manager's map (0x465590).
struct CharacterNode {
    uintptr_t links[2];
    CharacterNode* left;
    CharacterNode* right;
    int32_t id;
    uintptr_t character;
};

uint32_t setting = gameDefault;
GameImage game;
TweakLog logger("Enemy Pool Size");
uintptr_t requestCharacterAddress;
uintptr_t putBackAddress;
uintptr_t growAndAppendAddress;
uintptr_t missionReturnAddress;
using SetUpRespawn = uint32_t(__stdcall*)(uint8_t* respawn, const int32_t* ids, int32_t count, int32_t kills,
    uint32_t deferred);
SetUpRespawn originalSetUpRespawn;

// The enemies asked for and not yet given; touched on the game thread only.
int32_t pendingIds[maxExtraEnemies];
uint32_t pendingCount = 0;
uint8_t* pendingRespawn = nullptr;
uint32_t framesWaited = 0;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // push ebp; mov ebp,esp; sub esp,0x10; push ebx; push esi; mov esi,[ebp+8]
    const uint8_t setUp[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x53, 0x56, 0x8b, 0x75, 0x08};
    // push 0; push ecx (kills); push edx (count); lea ecx,[esi+0x18] (ids); push ecx; push eax
    const uint8_t missionCall[] = {0x6a, 0x00, 0x51, 0x52, 0x8d, 0x4e, 0x18, 0x51, 0x50};
    // push ebp; mov ebp,esp; mov edx,[ebp+8]; mov eax,[edx]; mov eax,[eax+0x24]; mov eax,[eax]
    const uint8_t request[] = {0x55, 0x8b, 0xec, 0x8b, 0x55, 0x08, 0x8b, 0x02, 0x8b, 0x40, 0x24, 0x8b, 0x00};
    // push ebp; mov ebp,esp; sub esp,0x10; push ebx; mov ebx,[eax]; push esi; mov esi,[ebx+0xc]; push edi;
    // mov byte [eax+8],0
    const uint8_t putBack[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x10, 0x53, 0x8b, 0x18, 0x56, 0x8b, 0x73, 0x0c, 0x57,
        0xc6, 0x40, 0x08, 0x00};
    // push ebp; mov ebp,esp; sub esp,8; mov ecx,[esi+4]; sub ecx,[esi]
    const uint8_t growAndAppend[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x8b, 0x4e, 0x04, 0x2b, 0x0e};
    // mov edx,0x1e (the first wait)
    const uint8_t firstWait[] = {0xba};
    return game.codeIs(setUpRespawn, setUp, sizeof(setUp)) &&
           game.codeIs(missionSetUpCall - sizeof(missionCall), missionCall, sizeof(missionCall)) &&
           game.callsTo(missionSetUpCall, setUpRespawn) &&
           game.codeIs(requestCharacterCode, request, sizeof(request)) &&
           game.codeIs(putBackCode, putBack, sizeof(putBack)) &&
           game.codeIs(growAndAppendCode, growAndAppend, sizeof(growAndAppend)) &&
           game.codeIs(firstWaitOperand - 1, firstWait, sizeof(firstWait));
}

uint8_t* runningRespawn() {
    const uintptr_t managers = game.readUint32(managersGlobal);
    if (!managers) return nullptr;
    return *reinterpret_cast<uint8_t**>(managers + respawnOffset);
}

RespawnList& listOf(uint8_t* respawn) { return *reinterpret_cast<RespawnList*>(respawn + respawnListOffset); }

// The character of id, as 0x465590 finds it, or 0 if the character manager has none.
uintptr_t findCharacter(int32_t id) {
    const uintptr_t manager = game.readUint32(characterManagerGlobal);
    if (!manager) return 0;
    const auto* end = reinterpret_cast<const CharacterNode*>(manager + 4);
    const CharacterNode* node = *reinterpret_cast<CharacterNode* const*>(manager + 8);
    const CharacterNode* found = end;
    while (node) {
        if (node->id < id) {
            node = node->right;
            continue;
        }
        found = node;
        node = node->left;
    }
    if (found == end || id < found->id) return 0;
    return found->character;
}

int32_t requestCharacter(int16_t definition) {
    float position[3];
    std::memcpy(position, reinterpret_cast<const void*>(game.address(zeroPosition)), sizeof(position));
    const float* positionAddress = position;
    const uint32_t manager = game.readUint32(managersGlobal);
    const int32_t definitionNumber = definition;
    int32_t id;
    __asm {
        mov ebx, positionAddress  // ebx = position
        mov ecx, definitionNumber  // ecx = definition
        push -1
        push 0  // flags, as the mission passes
        push manager
        call requestCharacterAddress
        mov id, eax
    }
    return id;
}

void putBack(RespawnEntry* entry, uint8_t* respawn) {
    __asm {
        mov eax, entry  // eax = entry
        push respawn
        call putBackAddress
    }
}

void growAndAppend(RespawnList* list, const RespawnEntry* entry) {
    RespawnEntry* end = list->end;
    __asm {
        mov esi, list  // esi = list, eax = where to insert
        mov eax, end
        push entry
        call growAndAppendAddress
    }
}

bool contains(const int32_t* ids, uint32_t count, int32_t id) {
    for (uint32_t index = 0; index < count; ++index)
        if (ids[index] == id) return true;
    return false;
}

bool isListed(uint8_t* respawn, uintptr_t character) {
    const RespawnList& list = listOf(respawn);
    for (const RespawnEntry* entry = list.begin; entry != list.end; ++entry)
        if (entry->character == character) return true;
    return false;
}

// As 0x436370 gives a mission's enemy: marked, listed as waiting, then hidden.
void giveEnemy(uint8_t* respawn, uintptr_t character) {
    const uintptr_t parameters = *reinterpret_cast<const uintptr_t*>(character + characterParameters);
    if (parameters) *reinterpret_cast<uint32_t*>(parameters + parameterFlags) |= flagRespawning;
    const RespawnEntry entry{character, static_cast<int32_t>(game.readUint32(firstWaitOperand)), 0, {}};
    RespawnList& list = listOf(respawn);
    if (list.end != list.capacityEnd) {
        *list.end = entry;
        ++list.end;
    } else {
        growAndAppend(&list, &entry);
    }
    RespawnEntry scratch = entry;  // The game passes a copy too; 0x437150 resets it
    putBack(&scratch, respawn);
}

void dropPending() {
    pendingCount = 0;
    pendingRespawn = nullptr;
}

void requestExtraEnemies(uint8_t* respawn, const int32_t* ids, int32_t count) {
    dropPending();
    const uintptr_t missionManager = game.readUint32(missionManagerGlobal);
    if (!missionManager) return;
    const uintptr_t row = *reinterpret_cast<const uintptr_t*>(missionManager + missionRowOffset);
    if (!row) return;
    int16_t definitions[rowEnemyCount];
    uint32_t definitionCount = 0;
    for (uint32_t index = 0; index < rowEnemyCount; ++index) {
        const int16_t definition = *reinterpret_cast<const int16_t*>(row + rowEnemyDefinitions + index * 2);
        if (definition != noDefinition) definitions[definitionCount++] = definition;
    }
    if (definitionCount == 0) return;

    const int32_t extraCount = poolSizes[setting] - count;
    for (int32_t index = 0; index < extraCount && pendingCount < maxExtraEnemies; ++index) {
        const int32_t id = requestCharacter(definitions[index % definitionCount]);
        // A definition with a fixed ID gives the same character again.
        if (id < 0 || contains(ids, count, id) || contains(pendingIds, pendingCount, id)) continue;
        pendingIds[pendingCount++] = id;
    }
    if (pendingCount == 0) return;
    pendingRespawn = respawn;
    framesWaited = 0;
}

uint32_t __stdcall setUpRespawnHook(uint8_t* respawn, const int32_t* ids, int32_t count, int32_t kills,
    uint32_t deferred) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint32_t result = originalSetUpRespawn(respawn, ids, count, kills, deferred);
    if (setting != gameDefault && setting < _countof(poolSizes) && caller == missionReturnAddress && respawn)
        requestExtraEnemies(respawn, ids, count);
    return result;
}

// Each frame: nothing to do unless a mission's extra enemies are on their way.
void WINAPI giveReadyEnemies(void*) {
    if (pendingCount == 0) return;
    uint8_t* respawn = runningRespawn();
    // Stopped (the mission ended) or set up again: the enemies stay as the game's other characters.
    if (respawn != pendingRespawn || *reinterpret_cast<const uint32_t*>(respawn + respawnStateOffset) != respawnRunning) {
        dropPending();
        return;
    }
    if (++framesWaited > maxWaitFrames) {
        char line[160];
        sprintf_s(line, "%u extra enemies were not made within %u frames; the mission goes on without them",
            pendingCount, maxWaitFrames);
        logger.write("WARN", line);
        dropPending();
        return;
    }
    // In the order asked for; the ones not made yet stay.
    uint32_t stillPending = 0;
    for (uint32_t index = 0; index < pendingCount; ++index) {
        const uintptr_t character = findCharacter(pendingIds[index]);
        if (!character || *reinterpret_cast<const uint32_t*>(character + characterState) != characterReady) {
            pendingIds[stillPending++] = pendingIds[index];
            continue;
        }
        if (!isListed(respawn, character)) giveEnemy(respawn, character);
    }
    pendingCount = stillPending;
    if (pendingCount == 0) pendingRespawn = nullptr;
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the respawning enemies of missions are not the known version", setUpRespawn);
        return false;
    }
    requestCharacterAddress = game.address(requestCharacterCode);
    putBackAddress = game.address(putBackCode);
    growAndAppendAddress = game.address(growAndAppendCode);
    missionReturnAddress = game.address(missionSetUpReturn);
    void* original = nullptr;
    if (CodeChangeError error = hookGameCode(context, game, setUpRespawn, setUpRespawnClaim,
            reinterpret_cast<void*>(setUpRespawnHook), &original)) {
        logger.installFailed(error, setUpRespawn);
        return false;
    }
    originalSetUpRespawn = reinterpret_cast<SetUpRespawn>(original);
    if (!context.host->onFrame(context.module, giveReadyEnemies, nullptr)) {
        unhookGameCode(game, setUpRespawn);
        logger.installFailed("the ModLoader did not accept the per-frame callback", setUpRespawn);
        return false;
    }
    return true;
}

const Text poolChoices[] = {
    {L"デフォルト", L"Default"},
    {L"16体", L"16"},
    {L"24体", L"24"},
};

const Option options[] = {
    choice(L"pool", {L"雑魚の候補数", L"Enemy Pool Size"}, poolChoices, &setting, gameDefault),
};

}  // namespace

const Tweak enemyPoolSizeTweak{L"enemy_pool_size", options, 1, install};

}  // namespace tweaks
