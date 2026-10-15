// Respawning enemies (AcAround.cpp, [[0x9e9640]+8]) are the same few characters brought back. Each
// frame 0x436a50 looks at the ones out: one is down if its state [chara+0x114] has 0x400000 (dead) or
// 0x800000 (left the battle). A down one waits 30 frames, then goes back to wait for a respawn
// (0x437150 hides it and empties what it carries) unless its body is on screen (0x435550). So the
// bodies in view hold the enemies back, and so do the ones still falling: 0x200000 (dying, as in a
// Spring Harvest) turns into 0x400000 only when the animation ends (0x494fb0).
#include "enemy_corpse_cleanup.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t downStateTest = 0x436abc;       // test dword ptr [edi+0x114], 0xc00000
constexpr uintptr_t downStateOperand = 0x436ac2;
constexpr uintptr_t onScreenJump = 0x436af9;        // jne (on screen: keep the body)
constexpr uintptr_t firstWaitOperand = 0x43646d;    // mov edx, 30 (0x436370, when the enemies are given)
constexpr uintptr_t laterWaitOperand = 0x437165;    // mov dword ptr [eax+4], 30 (0x437150, when one goes back)

constexpr uint32_t dyingState = 0x200000;
constexpr uint32_t deadState = 0x400000;
constexpr uint32_t leftBattleState = 0x800000;
constexpr uint32_t gameWaitFrames = 30;

enum CleanupChoice : uint32_t { gameDefault, offScreenAtOnce, alwaysAfterWait };

constexpr uint32_t dyingCountsAsDown = dyingState | deadState | leftBattleState;
constexpr uint32_t noWait = 0;
constexpr uint8_t nop = 0x90;

enum PatchIndex { downState, firstWait, laterWait, onScreen, patchCount };

// The operands are set from the choice in applied.
CodePatch patches[patchCount] = {
    CodePatch(downStateOperand, {0, 0, 0, 0}),
    CodePatch(firstWaitOperand, {0, 0, 0, 0}),
    CodePatch(laterWaitOperand, {0, 0, 0, 0}),
    CodePatch(onScreenJump, {nop, nop}),
};

uint32_t setting = gameDefault;
GameImage game;
TweakLog logger("Defeated Enemy Cleanup");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // test dword ptr [edi+0x114],0xc00000
    const uint8_t downTest[] = {0xf7, 0x87, 0x14, 0x01, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x00};
    // lea eax,[edi+0xd4]; push eax; mov edi,<global>; call 0x435550; add esp,4; test al,al; jne 0x436b47
    const uint8_t screenCheckStart[] = {0x8d, 0x87, 0xd4, 0x00, 0x00, 0x00, 0x50, 0xbf};
    const uint8_t screenCheckEnd[] = {0x83, 0xc4, 0x04, 0x84, 0xc0, 0x75, 0x4c};
    // mov eax,[esi+4]; mov edx,0x1e; mov [ebp-0xc],ecx
    const uint8_t firstWait[] = {0x8b, 0x46, 0x04, 0xba, 0x1e, 0x00, 0x00, 0x00, 0x89, 0x4d, 0xf4};
    // mov byte ptr [eax+8],0; mov dword ptr [eax+4],0x1e
    const uint8_t laterWait[] = {0xc6, 0x40, 0x08, 0x00, 0xc7, 0x40, 0x04, 0x1e, 0x00, 0x00, 0x00};
    return game.codeIs(downStateTest, downTest, sizeof(downTest)) &&
           game.codeIs(0x436ae3, screenCheckStart, sizeof(screenCheckStart)) && game.callsTo(0x436aef, 0x435550) &&
           game.codeIs(onScreenJump - 5, screenCheckEnd, sizeof(screenCheckEnd)) &&
           game.codeIs(firstWaitOperand - 4, firstWait, sizeof(firstWait)) &&
           game.codeIs(laterWaitOperand - 7, laterWait, sizeof(laterWait));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the clearing of defeated enemies is not the known version", downStateTest);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void setOperand(PatchIndex patch, uint32_t value) {
    patches[patch].setPatchedBytes(reinterpret_cast<const uint8_t*>(&value));  // x86: little-endian
}

void applied(uint32_t value) {
    if (!installed) return;
    if (value > alwaysAfterWait) value = gameDefault;
    // Clearing a body in view at once would take it away as it falls; it keeps the game's wait.
    const uint32_t waitFrames = value == alwaysAfterWait ? gameWaitFrames : noWait;
    setOperand(downState, dyingCountsAsDown);
    setOperand(firstWait, waitFrames);
    setOperand(laterWait, waitFrames);
    const bool changed = value != gameDefault;
    const bool patched[patchCount] = {changed, changed, changed, value == alwaysAfterWait};
    for (int patch = 0; patch < patchCount; ++patch) writePatches(&patches[patch], 1, patched[patch], logger);
}

const Text cleanupChoices[] = {
    {L"デフォルト", L"Default"},
    {L"画面外ならすぐ", L"Off Screen at Once"},
    {L"画面内でも消す", L"Even On Screen"},
};

const Option options[] = {
    choice(L"cleanup", {L"倒した雑魚の片付け", L"Defeated Enemy Cleanup"}, cleanupChoices, &setting, gameDefault,
        applied),
};

}  // namespace

const Tweak enemyCorpseCleanupTweak{L"enemy_corpse_cleanup", options, 1, install};

}  // namespace tweaks
