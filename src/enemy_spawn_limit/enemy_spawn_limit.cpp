// Respawning enemies (AcAround.cpp, [[0x9e9640]+8]) come back only while fewer than [+0x30] are
// alive. The battle scripts' SetWakiZako sets it to the number of enemies they give, but a mission
// (0x50ce30 -> 0x436370) leaves the 6 that stopping the last one wrote (0x436500), while it makes 8
// enemies (0x50d170, the row's +0x2c). Writing more than there are enemies changes nothing else:
// a spawn takes a waiting enemy, and none is left.
#include "enemy_spawn_limit.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t resetLimit = 0x4365e0;          // mov dword ptr [edi+0x30], 6 (in 0x436500)
constexpr uintptr_t resetLimitOperand = 0x4365e3;
constexpr uintptr_t managers = 0x9e9640;            // [managers]+8 is the respawning enemies
constexpr uintptr_t respawnOffset = 0x8;
constexpr uintptr_t aliveLimitOffset = 0x30;

constexpr uint32_t gameLimit = 6;
// More than SetWakiZako can give (31, 0x545a1b), so the tweak knows the value as its own.
constexpr uint32_t noLimit = 127;

CodePatch patches[] = {
    CodePatch(resetLimitOperand, {noLimit, 0, 0, 0}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("More Enemies at Once");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov [edi+0x3c],ebx; mov dword ptr [edi+0x30],6; mov [edi+0x40],bl
    const uint8_t limitReset[] = {0x89, 0x5f, 0x3c, 0xc7, 0x47, 0x30, 0x06, 0x00, 0x00, 0x00, 0x88, 0x5f, 0x40};
    return game.codeIs(resetLimit - 3, limitReset, sizeof(limitReset));
}

// The patch takes effect when the game next stops the respawning enemies (a mission or a battle
// ends, an area is entered). A mission already under way gets the new limit here.
void updateRunningLimit(bool unlimited) {
    const uint32_t managerList = game.readUint32(managers);
    if (managerList == 0) return;
    const auto respawn = *reinterpret_cast<const uint32_t*>(managerList + respawnOffset);
    if (respawn == 0) return;
    auto& aliveLimit = *reinterpret_cast<uint32_t*>(respawn + aliveLimitOffset);
    if (unlimited && aliveLimit == gameLimit) aliveLimit = noLimit;
    if (!unlimited && aliveLimit == noLimit) aliveLimit = gameLimit;
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the respawning of enemies is not the known version", resetLimit);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (!installed) return;
    writePatches(patches, _countof(patches), value == toggleOn, logger);
    updateRunningLimit(value == toggleOn);
}

const Option options[] = {
    toggle(L"enabled", {L"雑魚の同時出現数アップ", L"More Enemies at Once"}, &enabled, false, applied),
};

}  // namespace

const Tweak enemySpawnLimitTweak{L"enemy_spawn_limit", options, 1, install};

}  // namespace tweaks
