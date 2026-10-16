// Every loss of a weapon's durability goes through 0x693bf0 (esi = weapon, stack = amount and the
// character holding it, ret 8): it lowers the durability [weapon+0x58], shows a message at each
// hundred, and breaks the weapon at 0, returning 1. It is called from the characters' virtual
// functions for hits and guards (0x499070, 0x4991a0; guns too).
// - No Loss: returns 0 without changing anything when the character is the player (ID 0, as
//   0x465590 is asked for the player); the other characters' weapons wear and break as before.
// - Break at Once: any weapon's loss becomes 32767 and the game breaks the weapon itself. The game
//   scales the amount by the weapon's [+0xb4] (-10% to -100%) and by 0.1 in a Spring Harvest, so it
//   stays past the largest durability (2000), except for a weapon that the game lets lose nothing.
#include "weapon_durability.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t loseDurability = 0x693bf0;
constexpr uint32_t loseDurabilityClaim = 6;  // push ebp; mov ebp,esp; and esp,-0x40

enum DurabilityChoice : uint32_t { gameDefault, noLoss, breakAtOnce };
constexpr int32_t breakingAmount = 0x7fff;  // The amount is read as an int16
static_assert(breakingAmount == INT16_MAX, "the detour writes 0x7fff");

uint32_t setting = gameDefault;
GameImage game;
TweakLog logger("Weapon Durability");
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

// Runs on every hit and guard, so it only reads: the stack holds the return address, the amount
// and the character, whose parameters [+0xc] hold its ID [+4].
__declspec(naked) void loseDurabilityDetour() {
    __asm {
        mov eax, setting
        cmp eax, noLoss
        je keepIfPlayer
        cmp eax, breakAtOnce
        jne keepGameBehavior
        // A loss the game made 0 (a charm, for example) stays 0.
        movsx eax, word ptr [esp + 4]
        test eax, eax
        jle keepGameBehavior
        mov dword ptr [esp + 4], 0x7fff  // breakingAmount (inline assembly takes no constexpr)
        jmp keepGameBehavior
    keepIfPlayer:
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

const Text durabilityChoices[] = {
    {L"デフォルト", L"Default"},
    {L"減らない", L"No Loss"},
    {L"即破損", L"Break at Once"},
};

// 耐久 is "Durability" in the game's English menus.
const Option options[] = {
    choice(L"durability", {L"武器耐久値", L"Weapon Durability"}, durabilityChoices, &setting, gameDefault),
};

}  // namespace

const Tweak weaponDurabilityTweak{L"weapon_durability", options, 1, install};

}  // namespace tweaks
