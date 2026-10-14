// What the player has learned is kept per stance and move at game state +0x18394 + 0x80 * stance
// (0 not learned, 3 learned but not usable in My Style yet). The My Style screens read it in only
// three places; choosing, the confirmations and making the style look at the cards these build.
// The record itself is left as it is, so turning the option off undoes everything.
#include "custom_style_all_arts.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uint8_t nop = 0x90;

CodePatch patches[] = {
    // Building a stance's cards (0x657e30): jne -> jmp, past marking an unlearned move's card as
    // not learned (state 3). Mixed-Stance My Style builds the other stances' cards here too.
    CodePatch(0x657fc1, {0xeb, 0x3a}),
    // Choosing a stance (0x5d0dd0): je -> nops, so no stance is refused with UI 488
    // "you have not learned any moves" for want of a learned first move
    CodePatch(0x5d0e89, {nop, nop}),
    // Copying a style (0x5cec50): jne -> nops, so moves recorded as other than 1 are copied too
    CodePatch(0x5ced53, {nop, nop, nop, nop, nop, nop}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("All My Style Moves");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov bl,[eax] (the record); cmp bl,3; jne; xor bl,bl
    const uint8_t readRecord[] = {0x8a, 0x18, 0x80, 0xfb, 0x03, 0x75, 0x02, 0x32, 0xdb};
    // mov esi,[esp+0x18]; mov edi,[esp+0x10]; test bl,bl; jne 0x657ffd; mov eax,3 (the card's state)
    const uint8_t markUnlearned[] = {0x8b, 0x74, 0x24, 0x18, 0x8b, 0x7c, 0x24, 0x10, 0x84, 0xdb, 0x75, 0x3a, 0xb8,
        0x03, 0x00, 0x00, 0x00};
    // cmp byte [ecx],0; je 0x5d0e95; cmp byte [eax+6],0; jne 0x5d0f69 (a usable first move)
    const uint8_t stanceHasMove[] = {0x80, 0x39, 0x00, 0x74, 0x0a, 0x80, 0x78, 0x06, 0x00, 0x0f, 0x85, 0xd4, 0x00,
        0x00, 0x00};
    // lea eax,[eax+ecx+0x18394]; jmp; xor eax,eax; cmp byte [eax],1; jne 0x5cede3; movsx esi,[edx+2]
    const uint8_t copyLearned[] = {0x8d, 0x84, 0x08, 0x94, 0x83, 0x01, 0x00, 0xeb, 0x02, 0x33, 0xc0, 0x80, 0x38,
        0x01, 0x0f, 0x85, 0x8a, 0x00, 0x00, 0x00, 0x0f, 0xbf, 0x72, 0x02};
    return game.codeIs(0x657f4f, readRecord, sizeof(readRecord)) &&
           game.codeIs(0x657fb7, markUnlearned, sizeof(markUnlearned)) &&
           game.codeIs(0x5d0e86, stanceHasMove, sizeof(stanceHasMove)) &&
           game.codeIs(0x5ced45, copyLearned, sizeof(copyLearned));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the My Style screens are not the known version", 0x657f4f);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

// The cards are built when the screen opens or the stance changes, so a change applies from then.
void applied(uint32_t value) {
    if (installed) writePatches(patches, _countof(patches), value == toggleOn, logger);
}

// 俺流 is "My Style" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"俺流技全解除", L"All My Style Moves"}, &enabled, false, applied),
};

}  // namespace

const Tweak customStyleAllArtsTweak{L"custom_style_all_arts", options, 1, install};

}  // namespace tweaks
