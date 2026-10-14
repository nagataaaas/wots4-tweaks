// Owning a set of clothes is one bit per clothes number in the game state, checked by 0x4db260
// (edi = number, push game state; ret 4; al = owned). The character setup asks it in two
// places: whether a part (clothes, hairstyle, face) can be chosen (0x4758c0) and whether a
// character set can be (0x479b70; a set without its own number goes through 0x4758c0 for each
// part). Both check the DLC before and the unlock after, so the DLC check stays and All Unlocks
// patches the unlock check. Shops, mission rewards and the achievement count are left as they are.
#include "appearance_all.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t clothesOwned = 0x4db260;

// call 0x4db260 -> mov al,1; add esp,4: the call's result, and the push the call's ret 4 would
// remove. The test al,al after it sets the flags.
CodePatch patches[] = {
    CodePatch(0x47591a, {0xb0, 0x01, 0x83, 0xc4, 0x04}),  // A part (0x4758c0)
    CodePatch(0x479dd5, {0xb0, 0x01, 0x83, 0xc4, 0x04}),  // A character set (0x479b70)
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("All Appearances");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // jle; push ebx (game state); call 0x4db260; test al,al; je
    const uint8_t partCheck[] = {0x7e, 0x0a, 0x53, 0xe8};
    const uint8_t partResult[] = {0x84, 0xc0, 0x74, 0x0f};
    // test edx,edx; jle; mov eax,[0x9e967c]; push eax; mov edi,edx; call 0x4db260; test al,al; je
    const uint8_t setCheck[] = {0x85, 0xd2, 0x7e, 0x15, 0xa1};
    const uint8_t setPush[] = {0x50, 0x8b, 0xfa, 0xe8};
    const uint8_t setResult[] = {0x84, 0xc0, 0x0f, 0x84};
    const uint8_t ownedPrologue[] = {0x55, 0x8b, 0xec, 0xb8, 0xff, 0x00, 0x00, 0x00};
    return game.codeIs(0x475917, partCheck, sizeof(partCheck)) && game.callsTo(0x47591a, clothesOwned) &&
           game.codeIs(0x47591f, partResult, sizeof(partResult)) &&
           game.codeIs(0x479dc9, setCheck, sizeof(setCheck)) && game.operandIs(0x479dce, 0x9e967c) &&
           game.codeIs(0x479dd2, setPush, sizeof(setPush)) &&
           game.callsTo(0x479dd5, clothesOwned) && game.codeIs(0x479dda, setResult, sizeof(setResult)) &&
           game.codeIs(clothesOwned, ownedPrologue, sizeof(ownedPrologue));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the character setup's check of owned clothes is not the known version", 0x47591a);
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
    toggle(L"enabled", {L"全容姿使用可", L"All Appearances"}, &enabled, false, applied),
};

}  // namespace

const Tweak appearanceAllTweak{L"appearance_all", options, 1, install};

}  // namespace tweaks
