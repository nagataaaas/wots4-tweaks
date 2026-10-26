// Looting (0x493270) and a kick (0x51c620) mark a body that gave an item as searched (+0x325),
// and the item draw 0x4f50b0 takes the item it gave out of the body's belongings, or for a
// weapon marks that no weapon is left (+0x326). Without the marks and with the belongings kept,
// the next draw sees the body as it was before. The draw is shared with the item a townsperson
// drops on dying, so that body keeps the item too.
#include "loot_infinite.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t lootMarksSearched = 0x493465;  // mov byte [ecx+0x325],1 after looting gave an item
constexpr uintptr_t kickMarksSearched = 0x51cbd8;  // mov byte [eax+0x325],1 after a kick gave an item
// After the draw wrote the item: mov edx,[esi+0x1f0], the start of taking it from the body.
// Jumps to 0x4f52b1 (pop edi; pop esi; mov al,1; ...), where the draw returns that it gave one.
constexpr uintptr_t drawTakesItem = 0x4f51ec;

// nop dword [eax+0] in the place of each 7-byte mark; jmp 0x4f52b1 (rel 0xc0) and a nop.
CodePatch patches[] = {
    CodePatch(lootMarksSearched, {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00}),
    CodePatch(kickMarksSearched, {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00}),
    CodePatch(drawTakesItem, {0xe9, 0xc0, 0x00, 0x00, 0x00, 0x90}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Endless Loot");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov ecx,[ebx+0xc]; mov byte [ecx+0x325],1; mov dword [edi+0xf4],0 (the body looted)
    const uint8_t lootMark[] = {0x8b, 0x4b, 0x0c, 0xc6, 0x81, 0x25, 0x03, 0x00, 0x00, 0x01, 0xc7, 0x87, 0xf4, 0x00};
    // fstp [ebp-0x5c]; mov byte [eax+0x325],1; call 0x4f5390 (the item thrown from the body)
    const uint8_t kickMark[] = {0xd9, 0x5d, 0xa4, 0xc6, 0x80, 0x25, 0x03, 0x00, 0x00, 0x01, 0xe8, 0xac, 0x87, 0xfd,
        0xff};
    // mov [edx],cx (the item written); mov edx,[esi+0x1f0]; mov edi,[esi+0x1ec] (the belongings)
    const uint8_t drawTake[] = {0x66, 0x89, 0x0a, 0x8b, 0x96, 0xf0, 0x01, 0x00, 0x00, 0x8b, 0xbe, 0xec, 0x01, 0x00,
        0x00};
    // pop edi; pop esi; mov al,1; pop ebx: the draw's return of "an item was given"
    const uint8_t gaveItem[] = {0x5f, 0x5e, 0xb0, 0x01, 0x5b};
    return game.codeIs(0x493462, lootMark, sizeof(lootMark)) && game.codeIs(0x51cbd5, kickMark, sizeof(kickMark)) &&
           game.codeIs(0x4f51e9, drawTake, sizeof(drawTake)) && game.codeIs(0x4f52b1, gaveItem, sizeof(gaveItem));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("looting is not the known version", lootMarksSearched);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (installed) writePatches(patches, _countof(patches), value == toggleOn, logger);
}

const Option options[] = {
    toggle(L"enabled", {L"漁り無限", L"Endless Loot"}, &enabled, false, applied),
};

}  // namespace

const Tweak lootInfiniteTweak{L"loot_infinite", options, 1, install};

}  // namespace tweaks
