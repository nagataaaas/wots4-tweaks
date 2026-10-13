// Refining (SmithRefine.cpp) refuses "strengthen" and "harden" when the weapon's quality (+0x40)
// has reached its maximum (+0x42), with message 0x17. That is the only place that compares them;
// refining itself caps quality at 999, attack at 500 and durability at 2000. Past quality 20 the
// attack gain has no random range (quality + 1).
#include "refine_quality_limit.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t maxQualityJump = 0x61bb64;  // jl 0x61bb94 (refine) -> jmp, past message 0x17

CodePatch patches[] = {
    CodePatch(maxQualityJump, {0xeb, 0x2e}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Unlimited Refining");
bool installed = false;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov eax,[edi+0x9c] (weapon); mov cx,[eax+0x40]; cmp cx,[eax+0x42]; jl 0x61bb94;
    // mov ebx,0x17; call 0x573be0 (message)
    const uint8_t maxQualityCheck[] = {0x8b, 0x87, 0x9c, 0x00, 0x00, 0x00, 0x66, 0x8b, 0x48, 0x40, 0x66, 0x3b, 0x48,
        0x42, 0x7c, 0x2e, 0xbb, 0x17, 0x00, 0x00, 0x00, 0xe8, 0x70, 0x80, 0xf5, 0xff};
    return game.codeIs(0x61bb56, maxQualityCheck, sizeof(maxQualityCheck));
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the smithy's refining is not the known version", 0x61bb56);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (installed) writePatches(patches, _countof(patches), value == toggleOn, logger);
}

// 精錬 is "Refine" in the game's English menus.
const Option options[] = {
    toggle(L"enabled", {L"質上限突破", L"Unlimited Refining"}, &enabled, false, applied),
};

}  // namespace

const Tweak refineQualityLimitTweak{L"refine_quality_limit", options, 1, install};

}  // namespace tweaks
