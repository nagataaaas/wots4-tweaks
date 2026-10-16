// Every loss of a weapon's durability goes through 0x693bf0 (esi = weapon, stack = amount and the
// character holding it, ret 8): it lowers the durability [weapon+0x58], shows a message at each
// hundred, and breaks the weapon at 0, returning 1. It is called from the characters' virtual
// functions for hits and guards (0x499070, 0x4991a0). The tweak returns 0 without changing anything
// when the character is the player (ID 0, as 0x465590 is asked for the player); the other
// characters' weapons wear and break as before.
#include "weapon_durability.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t loseDurability = 0x693bf0;
constexpr uint32_t loseDurabilityClaim = 6;  // push ebp; mov ebp,esp; and esp,-0x40

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("No Durability Loss");
void* originalLoseDurability = nullptr;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // push ebp; mov ebp,esp; and esp,-0x40; push -1
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xc0, 0x6a, 0xff};
    // movzx eax, word ptr [esi+0x58] (the durability); xor dl,dl; movsx edi,ax
    const uint8_t readsDurability[] = {0x0f, 0xb7, 0x46, 0x58, 0x32, 0xd2, 0x0f, 0xbf, 0xf8};
    // mov [esi+0x58],eax (the lowered durability)
    const uint8_t writesDurability[] = {0x89, 0x46, 0x58};
    // ret 8
    const uint8_t returns[] = {0xc2, 0x08, 0x00};
    return game.codeIs(loseDurability, prologue, sizeof(prologue)) &&
           game.codeIs(0x693c26, readsDurability, sizeof(readsDurability)) &&
           game.codeIs(0x693d3a, writesDurability, sizeof(writesDurability)) &&
           game.codeIs(0x693e85, returns, sizeof(returns));
}

// Runs on every hit and guard, so it only reads: the character (second stack argument, after the
// return address and the amount), its parameters [+0xc] and their ID [+4].
__declspec(naked) void loseDurabilityDetour() {
    __asm {
        cmp enabled, 0
        je keepGameBehavior
        mov eax, [esp + 8]
        test eax, eax
        je keepGameBehavior
        mov eax, [eax + 0xc]
        test eax, eax
        je keepGameBehavior
        cmp dword ptr [eax + 4], 0
        jne keepGameBehavior
        xor al, al  // Not broken
        ret 8
    keepGameBehavior:
        jmp originalLoseDurability
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the loss of weapon durability is not the known version", loseDurability);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, loseDurability, loseDurabilityClaim,
            reinterpret_cast<void*>(loseDurabilityDetour), &originalLoseDurability)) {
        logger.installFailed(error, loseDurability);
        return false;
    }
    return true;
}

// 耐久 is "Durability" in the game's English menus.
const Option options[] = {
    toggle(L"enabled", {L"耐久値減らない", L"No Durability Loss"}, &enabled, false),
};

}  // namespace

const Tweak weaponDurabilityTweak{L"weapon_durability", options, 1, install};

}  // namespace tweaks
