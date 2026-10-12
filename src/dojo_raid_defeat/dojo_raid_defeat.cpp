// With challengers in, every area change runs 0x5acbb0 (TsManager.cpp): one roll r = rand() % 100
// decides it. With the sensei as master of the dojo, r < 80 repels them (UI 720); otherwise the
// pupils repel them when r < the rate of the dojo's reputation (+0x14 of its row, 0 to 20).
// That one read of rand goes to this tweak's function instead.
#include "dojo_raid_defeat.h"
#include "../game_code.h"

namespace tweaks {

namespace {

enum DefeatSetting : uint32_t {
    defeatAsTheGameDecides = 0,
    defeatAlways = 1,
    defeatNever = 2,
};

// -1 % 100 is -1 with idiv, below both 80 and any rate (even 0); 99 is at or above them all.
constexpr int alwaysRepelRoll = -1;
constexpr int neverRepelRoll = 99;

constexpr uintptr_t defeatRoll = 0x5acbb5;  // call [rand] at the start of 0x5acbb0
constexpr uint8_t callOpcode = 0xff, callModRm = 0x15;

CodePatch patches[] = {CodePatch(defeatRoll, {0, 0, 0, 0, 0, 0})};

uint32_t setting = defeatAsTheGameDecides;
GameImage game;
TweakLog logger("Repelling Challengers");
RandFunction originalRand;
RandFunction defeatRand;  // What the patched call reads

int __cdecl rollForDefeat() {
    if (setting == defeatAlways) return alwaysRepelRoll;
    if (setting == defeatNever) return neverRepelRoll;
    return originalRand();
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x51, 0x53};  // push ebp; mov ebp,esp; push ecx; push ebx
    const uint8_t remainder[] = {0x99, 0xb9, 0x64, 0x00, 0x00, 0x00, 0xf7, 0xf9};  // cdq; mov ecx,100; idiv ecx
    const uint8_t keepRoll[] = {0x8b, 0xda, 0x89, 0x5d, 0xfc};  // mov ebx,edx; mov [ebp-4],ebx
    const uint8_t senseiCheck[] = {0x83, 0xfb, 0x50, 0x7d, 0x62};  // cmp ebx,80; jge
    // movsx ecx,word [eax+0x14]; cmp [ebp-4],ecx; jge (the pupils' rate)
    const uint8_t pupilsCheck[] = {0x0f, 0xbf, 0x48, 0x14, 0x39, 0x4d, 0xfc, 0x7d, 0x0e};
    return game.codeIs(0x5acbb0, prologue, sizeof(prologue)) && readsRand(game, defeatRoll, callOpcode, callModRm) &&
           game.codeIs(0x5acbbb, remainder, sizeof(remainder)) && game.codeIs(0x5acbd9, keepRoll, sizeof(keepRoll)) &&
           game.codeIs(0x5acc0d, senseiCheck, sizeof(senseiCheck)) &&
           game.codeIs(0x5acc5d, pupilsCheck, sizeof(pupilsCheck));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the check for repelling challengers is not the known version", defeatRoll);
        return false;
    }
    originalRand = gameRand(game);
    defeatRand = rollForDefeat;
    readRandFrom(patches[0], game, &defeatRand);
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    // Written once and kept: rollForDefeat reads the option each time.
    writePatches(patches, _countof(patches), true, logger);
    return true;
}

// 道場破り is "challenger" in the game's messages (「道場破りは弟子が撃退した！」 "Your pupils have
// repelled the attackers!").
const Text defeatChoices[] = {
    {L"デフォルト", L"Default"},
    {L"自動撃破", L"Always"},
    {L"撃破しない", L"Never"},
};
const Option options[] = {
    choice(L"defeat", {L"道場破り撃破", L"Repelling Challengers"}, defeatChoices, &setting, defeatAsTheGameDecides),
};

}  // namespace

const Tweak dojoRaidDefeatTweak{L"dojo_raid_defeat", options, 1, install};

}  // namespace tweaks
