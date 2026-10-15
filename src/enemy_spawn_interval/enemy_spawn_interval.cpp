// Respawning enemies (AcAround.cpp, [[0x9e9640]+8]) are used by the missions that ask for a number
// of kills and by the battle scripts' SetWakiZako. Each frame 0x436ca0 counts up [+0x38] and, once it
// has reached 30, brings one waiting enemy back if fewer than [+0x30] are alive, then starts again
// from 0: one enemy every 31 frames. The tweak changes the 30.
#include "enemy_spawn_interval.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t spawnWaitCompare = 0x436cb9;   // cmp eax, 30 (frames counted since the last spawn)
constexpr uintptr_t spawnWaitOperand = 0x436cbb;   // its 8-bit immediate

enum IntervalChoice : uint32_t { gameDefault, halfSecond, fifthSecond, tenthSecond };

// The game runs at 30 frames a second, and a spawn takes one frame more than the count compared.
constexpr uint8_t countBeforeSpawn(uint32_t framesPerSpawn) { return static_cast<uint8_t>(framesPerSpawn - 1); }
constexpr uint8_t countsBeforeSpawn[] = {
    countBeforeSpawn(31),  // the game's 30
    countBeforeSpawn(15),
    countBeforeSpawn(6),
    countBeforeSpawn(3),
};

CodePatch patches[] = {
    CodePatch(spawnWaitOperand, {countsBeforeSpawn[gameDefault]}),
};

uint32_t setting = gameDefault;
GameImage game;
TweakLog logger("Enemy Spawn Interval");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov eax,[esi+0x38]; cmp eax,0x1e; jge (spawn)
    const uint8_t spawnWait[] = {0x8b, 0x46, 0x38, 0x83, 0xf8, 0x1e, 0x7d, 0x09};
    return game.codeIs(spawnWaitCompare - 3, spawnWait, sizeof(spawnWait));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the respawning of enemies is not the known version", spawnWaitCompare);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (!installed) return;
    if (value >= _countof(countsBeforeSpawn)) value = gameDefault;
    patches[0].setPatchedBytes(&countsBeforeSpawn[value]);
    writePatches(patches, _countof(patches), value != gameDefault, logger);
}

const Text intervalChoices[] = {
    {L"デフォルト", L"Default"},
    {L"0.5秒", L"0.5 s"},
    {L"0.2秒", L"0.2 s"},
    {L"0.1秒", L"0.1 s"},
};

const Option options[] = {
    choice(L"interval", {L"雑魚の湧く間隔", L"Enemy Spawn Interval"}, intervalChoices, &setting, gameDefault,
        applied),
};

}  // namespace

const Tweak enemySpawnIntervalTweak{L"enemy_spawn_interval", options, 1, install};

}  // namespace tweaks
