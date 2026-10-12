// Without challengers in, every area change runs 0x5acb10 (TsManager.cpp). It rolls only when
// the player runs the dojo with pupils, it is not night and the player is not leaving the dojo;
// then challengers come when rand() % 100 < the rate of the dojo's reputation (+0x16 of its
// row: 18, or 0 at the lowest reputation). That one read of rand goes to this tweak's function.
// The conditions before it stay, so "always" only means always when the game would roll.
#include "dojo_raid_appearance.h"
#include "../game_code.h"

namespace tweaks {

namespace {

enum AppearanceSetting : uint32_t {
    appearanceAsTheGameDecides = 0,
    appearanceAlways = 1,
    appearanceNever = 2,
};

// 0 is below any rate above 0; 99 is at or above every rate (at most 20 in ReputationData.l).
constexpr int alwaysComeRoll = 0;
constexpr int neverComeRoll = 99;

constexpr uintptr_t appearanceRoll = 0x5acb72;  // call [rand] in 0x5acb10
constexpr uint8_t callOpcode = 0xff, callModRm = 0x15;

CodePatch patches[] = {CodePatch(appearanceRoll, {0, 0, 0, 0, 0, 0})};

uint32_t setting = appearanceAsTheGameDecides;
GameImage game;
TweakLog logger("Challenger Appearance");
RandFunction originalRand;
RandFunction appearanceRand;  // What the patched call reads

int __cdecl rollForAppearance() {
    if (setting == appearanceAlways) return alwaysComeRoll;
    if (setting == appearanceNever) return neverComeRoll;
    return originalRand();
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // cdq; mov ecx,100; idiv ecx; movsx eax,word [esi+0x16]; cmp edx,eax; jge (no challengers)
    const uint8_t rollAgainstRate[] = {0x99, 0xb9, 0x64, 0x00, 0x00, 0x00, 0xf7, 0xf9, 0x0f, 0xbf, 0x46, 0x16, 0x3b,
        0xd0, 0x7d, 0x18};
    const uint8_t challengersCome[] = {0xc6, 0x87, 0x7c, 0x40, 0x01, 0x00, 0x01};  // mov byte [edi+0x1407c],1
    // Not at night (+0x2c == 2), not leaving the dojo (+0x48b99 == 8)
    const uint8_t conditions[] = {0x80, 0x7f, 0x2c, 0x02, 0x74, 0x6a, 0x80, 0xbf, 0x99, 0x8b, 0x04, 0x00, 0x08};
    return readsRand(game, appearanceRoll, callOpcode, callModRm) &&
           game.codeIs(0x5acb78, rollAgainstRate, sizeof(rollAgainstRate)) &&
           game.codeIs(0x5acb88, challengersCome, sizeof(challengersCome)) &&
           game.codeIs(0x5acb30, conditions, sizeof(conditions));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the roll for challengers is not the known version", appearanceRoll);
        return false;
    }
    originalRand = gameRand(game);
    appearanceRand = rollForAppearance;
    readRandFrom(patches[0], game, &appearanceRand);
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    // Written once and kept: rollForAppearance reads the option each time.
    writePatches(patches, _countof(patches), true, logger);
    return true;
}

// 道場破り is "challenger" in the game's messages (「道場破りが現れた！」 "A challenger has appeared!").
const Text appearanceChoices[] = {
    {L"デフォルト", L"Default"},
    {L"確定出現", L"Always"},
    {L"出現しない", L"Never"},
};
const Option options[] = {
    choice(L"appearance", {L"道場破り出現率", L"Challenger Appearance"}, appearanceChoices, &setting,
        appearanceAsTheGameDecides),
};

}  // namespace

const Tweak dojoRaidAppearanceTweak{L"dojo_raid_appearance", options, 1, install};

}  // namespace tweaks
