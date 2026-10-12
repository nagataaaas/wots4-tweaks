// At the smithy, only "make it stronger" is random: refining (SmithRefine.cpp, 0x61c900) adds
// low + rand() % n to the attack, n being 6, 20 or 1 by the weapon's quality, and recasting
// (SmithReCast.cpp, 0x61a940) adds 10 + rand() % 6. With the smith Dojima, both add
// (rand() & 1) + 1 more. These three reads of rand go to this tweak's function instead, which
// returns a roll that is the largest for every one of them. Nothing else calls these reads.
#include "smith_best_roll.h"
#include "../game_code.h"

namespace tweaks {

namespace {

// 32759 % 60 == 59, and every divisor here (1, 2, 6, 20) divides 60, so the remainder is the
// largest each time; it is odd for Dojima's & 1. The largest such value up to RAND_MAX 0x7fff.
constexpr int bestRoll = 32759;
static_assert(bestRoll % 60 == 59 && bestRoll <= 0x7fff && bestRoll + 60 > 0x7fff, "the largest roll for 1, 2, 6, 20");

constexpr uint8_t movEbxOpcode = 0x8b, movEbxModRm = 0x1d;  // mov ebx,[rand]
constexpr uint8_t callOpcode = 0xff, callModRm = 0x15;      // call [rand]

CodePatch patches[] = {
    CodePatch(0x61ca17, {0, 0, 0, 0, 0, 0}),  // Refining: mov ebx,[rand], called for the roll and for Dojima
    CodePatch(0x61a9fc, {0, 0, 0, 0, 0, 0}),  // Recasting: call [rand] for the roll
    CodePatch(0x61aa23, {0, 0, 0, 0, 0, 0}),  // Recasting: call [rand] for Dojima
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Best Smithing");
RandFunction originalRand;
RandFunction smithRand;  // What the patched instructions read

int __cdecl rollForSmithing() {
    return enabled == toggleOn ? bestRoll : originalRand();
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov esi,eax; call ebx; sub edi,esi; inc edi; cdq; idiv edi (rand() % (high - low + 1))
    const uint8_t refineRoll[] = {0x8b, 0xf0, 0xff, 0xd3, 0x2b, 0xfe, 0x47, 0x99, 0xf7, 0xff};
    const uint8_t refineDojima[] = {0xff, 0xd3, 0x25, 0x01, 0x00, 0x00, 0x80};  // call ebx; and eax,0x80000001
    const uint8_t recastRoll[] = {0x99, 0xb9, 0x06, 0x00, 0x00, 0x00, 0xf7, 0xf9};  // cdq; mov ecx,6; idiv ecx
    const uint8_t recastDojima[] = {0x25, 0x01, 0x00, 0x00, 0x80};
    const uint8_t recastBase[] = {0x8d, 0x5a, 0x0a};  // lea ebx,[edx+10]
    // The quality thresholds 5 and 20 that choose the refining divisor
    const uint8_t lowQualityLimit[] = {0xc7, 0x44, 0x24, 0x20, 0x05, 0x00, 0x00, 0x00};
    const uint8_t middleQualityLimit[] = {0xc7, 0x44, 0x24, 0x24, 0x14, 0x00, 0x00, 0x00};
    return readsRand(game, 0x61ca17, movEbxOpcode, movEbxModRm) &&
           game.codeIs(0x61ca1d, refineRoll, sizeof(refineRoll)) &&
           game.codeIs(0x61ca40, refineDojima, sizeof(refineDojima)) &&
           readsRand(game, 0x61a9fc, callOpcode, callModRm) && game.codeIs(0x61aa02, recastRoll, sizeof(recastRoll)) &&
           readsRand(game, 0x61aa23, callOpcode, callModRm) &&
           game.codeIs(0x61aa29, recastDojima, sizeof(recastDojima)) &&
           game.codeIs(0x61aa0f, recastBase, sizeof(recastBase)) &&
           game.codeIs(0x61c9b3, lowQualityLimit, sizeof(lowQualityLimit)) &&
           game.codeIs(0x61c9c1, middleQualityLimit, sizeof(middleQualityLimit));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the smithy's refining and recasting are not the known version", 0x61ca17);
        return false;
    }
    originalRand = gameRand(game);
    smithRand = rollForSmithing;
    for (CodePatch& patch : patches) readRandFrom(patch, game, &smithRand);
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    // Written once and kept: rollForSmithing reads the option each time.
    writePatches(patches, _countof(patches), true, logger);
    return true;
}

// 鍛冶屋 is "Smithy" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"鍛冶結果最高", L"Best Smithing"}, &enabled, false),
};

}  // namespace

const Tweak smithBestRollTweak{L"smith_best_roll", options, 1, install};

}  // namespace tweaks
