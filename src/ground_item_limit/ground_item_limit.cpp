// Things on the ground (ItemEntitySpawner.cpp, [0x9e9694]) have 60 places and two counts, byte [+4]
// and byte [+5] (m_WeaponSpawnCount and m_ItemSpawnCount in its asserts). Every way of placing one
// first calls 0x4f7740, which removes the oldest of the same kind when that count is over 30 (the
// count includes the new one) or no place is free. The tweak raises the 30 at once, and the places
// (ground_slots) to twice the limit from the next start, as the game has 30 of each in 60 places.
// The counts are signed bytes: the game asserts they stay >= 0 and stops with a message box once one
// is past 127, so a count may reach the limit + 1 but no more than 127.
#include "ground_item_limit.h"
#include "../game_code.h"
#include "ground_slots.h"

namespace tweaks {

namespace {

constexpr uintptr_t limitLoad = 0x4f7795;      // mov cl, 30 (compared with each count, signed)
constexpr uintptr_t limitOperand = 0x4f7796;

enum LimitChoice : uint32_t { gameDefault, sixty, oneTwenty };

constexpr uint8_t limits[] = {30, 60, 120};
constexpr uint32_t placesPerLimit = 2;  // The weapons and the items share the places
static_assert(ground_slots::gamePlaceCount == limits[gameDefault] * placesPerLimit, "the game's 30 in 60 places");

CodePatch patches[] = {
    CodePatch(limitOperand, {limits[gameDefault]}),
};

uint32_t setting = gameDefault;
GameImage game;
TweakLog logger("Dropped Item Limit");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // lea esi,[edi+0x18]; mov [ebp-0xc],esi; mov dword ptr [ebp-8],12 (groups of 5 places); mov cl,0x1e
    const uint8_t limitSetup[] = {0x8d, 0x77, 0x18, 0x89, 0x75, 0xf4, 0xc7, 0x45, 0xf8, 0x0c, 0x00, 0x00, 0x00,
        0xb1, 0x1e};
    return game.codeIs(limitLoad - 13, limitSetup, sizeof(limitSetup));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the placing of dropped things is not the known version", limitLoad);
        return false;
    }
    if (!ground_slots::install(context, game, logger)) return false;
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

// The limit changes at once; the places only at the first call, as the game starts.
void applied(uint32_t value) {
    if (!installed) return;
    if (value >= _countof(limits)) value = gameDefault;
    ground_slots::applyAtStartup(limits[value] * placesPerLimit);
    patches[0].setPatchedBytes(&limits[value]);
    writePatches(patches, _countof(patches), value != gameDefault, logger);
}

const Text limitChoices[] = {
    {L"デフォルト", L"Default"},
    {L"60個", L"60"},
    {L"120個", L"120"},
};

const Option options[] = {
    restartChoice(L"limit", {L"ドロップ品の上限", L"Dropped Item Limit"}, limitChoices, &setting, gameDefault,
        {L"変更はゲームを再起動した後にすべて有効になります", L"The change fully takes effect after restarting the game"},
        applied),
};

}  // namespace

const Tweak groundItemLimitTweak{L"ground_item_limit", options, 1, install};

}  // namespace tweaks
