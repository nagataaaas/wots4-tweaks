// The My Style screens copy the ability points (game state +0x18a16) to their shared data +0x52
// and keep the points of the chosen moves in +0x50 (both int16). Only these four places compare
// them; making My Style, saving and fighting do not look at the points, and the points are
// never spent. The patches let every comparison pass; the screens still show "total / points".
#include "custom_style_limit.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uint32_t unlimitedBudget = 0x7fffffff;
constexpr uint8_t budgetLow = unlimitedBudget & 0xff;
constexpr uint8_t budgetHigh = unlimitedBudget >> 24;

CodePatch patches[] = {
    // Choosing one move (CreateSectSkillSelect.cpp): jle 0x5cb832 -> jmp, past UI 701 "more points needed"
    CodePatch(0x5cb742, {0xe9, 0xeb, 0x00, 0x00, 0x00, 0x90}),
    // Choosing moves at once: budget = points - total -> movsx ecx,[ecx+0x50]; mov eax,0x7fffffff
    CodePatch(0x5cbad1, {0x0f, 0xbf, 0x49, 0x50, 0xb8, budgetLow, 0xff, 0xff, budgetHigh, 0x90}),
    // Copying a style (CreateSectStyleCopy.cpp): the same budget
    CodePatch(0x5ceb35, {0x0f, 0xbf, 0x49, 0x50, 0xb8, budgetLow, 0xff, 0xff, budgetHigh, 0x90}),
    // Confirming the new style: jge 0x5d3b39 -> jmp, past UI 701
    CodePatch(0x5d3b30, {0xeb, 0x07}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Unlimited My Style");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // movsx edx,[eax+0x52]; movsx eax,[eax+0x50]; sub eax,edx; add eax,ecx; test eax,eax; jle 0x5cb832
    const uint8_t chooseOne[] = {0x0f, 0xbf, 0x50, 0x52, 0x0f, 0xbf, 0x40, 0x50, 0x2b, 0xc2, 0x03, 0xc1, 0x85, 0xc0,
        0x0f, 0x8e, 0xea, 0x00, 0x00, 0x00};
    // call 0x5c9f80 (count the total); mov ecx,[ebx+0x34]; movsx eax,[ecx+0x52]; movsx ecx,[ecx+0x50]; sub eax,ecx
    const uint8_t chooseAtOnce[] = {0xe8, 0xb2, 0xe4, 0xff, 0xff, 0x8b, 0x4b, 0x34, 0x0f, 0xbf, 0x41, 0x52, 0x0f, 0xbf,
        0x49, 0x50, 0x2b, 0xc1, 0x46, 0x83, 0xfe, 0x03, 0x77, 0x2a};
    // The same with mov ecx,[esi+0x34], then push eax ... call 0x5cec50 (copy)
    const uint8_t copyStyle[] = {0xe8, 0x4e, 0xb4, 0xff, 0xff, 0x8b, 0x4e, 0x34, 0x0f, 0xbf, 0x41, 0x52, 0x0f, 0xbf,
        0x49, 0x50, 0x2b, 0xc1, 0x50, 0x53, 0x57, 0x56, 0xe8};
    // mov ax,[ecx+0x52]; cmp ax,[ecx+0x50]; jge 0x5d3b39
    const uint8_t confirm[] = {0x66, 0x8b, 0x41, 0x52, 0x66, 0x3b, 0x41, 0x50, 0x7d, 0x07};
    // +0x52 is the copy of the points: mov dx,[ebx+0x18a16]; mov [esi+0x4a],dx (esi = shared data +8)
    const uint8_t copyPoints[] = {0x66, 0x8b, 0x93, 0x16, 0x8a, 0x01, 0x00, 0x66, 0x89, 0x56, 0x4a};
    return game.codeIs(0x5cb734, chooseOne, sizeof(chooseOne)) &&
           game.codeIs(0x5cbac9, chooseAtOnce, sizeof(chooseAtOnce)) &&
           game.codeIs(0x5ceb2d, copyStyle, sizeof(copyStyle)) && game.codeIs(0x5d3b28, confirm, sizeof(confirm)) &&
           game.codeIs(0x5c9996, copyPoints, sizeof(copyPoints));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the My Style screens are not the known version", 0x5cb734);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (installed) writePatches(patches, _countof(patches), value == toggleOn, logger);
}

// 俺流 is "My Style" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"俺流上限突破", L"Unlimited My Style"}, &enabled, false, applied),
};

}  // namespace

const Tweak customStyleLimitTweak{L"custom_style_limit", options, 1, install};

}  // namespace tweaks
