// Hooks the two rolls inside the item draw 0x4f50b0 (see loot_roll.h). The draw is also used
// when a townsperson dies and drops an item by itself; that case is told apart by the draw's
// return address and left as the game has it.
#include "loot_drop.h"
#include "../game_code.h"
#include "loot_roll.h"

namespace tweaks {

namespace {

using namespace loot_drop;

constexpr uintptr_t itemDraw = 0x4f50b0;
// mov ecx,0x64 (5 bytes) right before each idiv: eax holds rand(), ebx the drop rate.
constexpr uintptr_t dropRateDivisor = 0x4f5173;
constexpr uintptr_t candidateRollDivisor = 0x4f5190;
constexpr uint32_t divisorLength = 5;
// The draw's callers and the addresses they return to.
constexpr uintptr_t lootCall = 0x493354;  // Looting a body (0x493270)
constexpr uintptr_t kickCall = 0x51cbaf;  // A kick landing on a body (0x51c620)
constexpr uint32_t callLength = 5;
constexpr uintptr_t randImport = 0x9052f8;

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Guaranteed Loot");
uintptr_t lootReturn;  // Relocated at install, so the hooks compare without arithmetic
uintptr_t kickReturn;
void* originalDropRateDivisor;
void* originalCandidateRollDivisor;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // push ebp; mov ebp,esp; sub esp,0x5c: [ebp+4] is the return address all through the draw
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x5c};
    const uint8_t loadRand[] = {0x8b, 0x3d};  // mov edi,[rand]
    // The rate row by difficulty, call rand; cdq; mov ecx,100; idiv; cmp edx,ebx; jge miss,
    // the candidates (0x4f57f0), call rand; cdq; mov ecx,100; idiv
    const uint8_t rolls[] = {0x8b, 0xd0, 0xc1, 0xe2, 0x05, 0x2b, 0xd0, 0x0f, 0xb6, 0x5c, 0x51, 0x02, 0x8d, 0x74,
        0x51, 0x02, 0xff, 0xd7, 0x99, 0xb9, 0x64, 0x00, 0x00, 0x00, 0xf7, 0xf9, 0x3b, 0xd3, 0x7d, 0x3d, 0x8b, 0x45,
        0xa4, 0x8d, 0x55, 0xbc, 0x52, 0x50, 0x8b, 0xc6, 0xe8, 0x63, 0x06, 0x00, 0x00, 0xff, 0xd7, 0x99, 0xb9, 0x64,
        0x00, 0x00, 0x00, 0xf7, 0xf9};
    const uint8_t firstWeight[] = {0x8d, 0x4d, 0xc0};  // lea ecx,[ebp-0x40]
    return game.codeIs(itemDraw, prologue, sizeof(prologue)) && game.codeIs(0x4f515a, loadRand, sizeof(loadRand)) &&
           game.operandIs(0x4f515c, randImport) && game.codeIs(0x4f5160, rolls, sizeof(rolls)) &&
           game.codeIs(0x4f519b, firstWeight, sizeof(firstWeight)) && game.callsTo(lootCall, itemDraw) &&
           game.callsTo(kickCall, itemDraw);
}

bool __cdecl forcesDrop(uintptr_t drawReturn) {
    return enabled == toggleOn && (drawReturn == lootReturn || drawReturn == kickReturn);
}

uint32_t __cdecl candidateRoll(const uint8_t* frame, uint32_t roll) {
    return rollWithinCandidates(frame, roll);
}

// Offsets of the registers saved by pushad.
enum PushadSlot { savedEbx = 0x10, savedEdx = 0x14, savedEax = 0x1c };
enum InlineAsmConstant : uint32_t { forcedDropRate = certainDropRate };  // Inline asm takes enums, not constexpr

__declspec(naked) void dropRateDivisorHook() {
    __asm {
        pushad
        push dword ptr [ebp + 4]
        call forcesDrop
        add esp, 4
        test al, al
        jz keepRate
        mov dword ptr [esp + savedEbx], forcedDropRate
    keepRate:
        popad
        jmp dword ptr [originalDropRateDivisor]
    }
}

__declspec(naked) void candidateRollDivisorHook() {
    __asm {
        pushad
        push dword ptr [ebp + 4]
        call forcesDrop
        add esp, 4
        test al, al
        jz keepRoll
        push dword ptr [esp + savedEax]
        push ebp
        call candidateRoll
        add esp, 8
        mov dword ptr [esp + savedEax], eax
        mov dword ptr [esp + savedEdx], 0  // The cdq of a smaller non-negative roll
    keepRoll:
        popad
        jmp dword ptr [originalCandidateRollDivisor]
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the item draw is not the known version", itemDraw);
        return false;
    }
    lootReturn = game.address(lootCall + callLength);
    kickReturn = game.address(kickCall + callLength);

    if (CodeChangeError error = hookGameCode(context, game, dropRateDivisor, divisorLength,
            reinterpret_cast<void*>(&dropRateDivisorHook), &originalDropRateDivisor)) {
        logger.installFailed(error, dropRateDivisor);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, candidateRollDivisor, divisorLength,
            reinterpret_cast<void*>(&candidateRollDivisorHook), &originalCandidateRollDivisor)) {
        unhookGameCode(game, dropRateDivisor);
        logger.installFailed(error, candidateRollDivisor);
        return false;
    }
    return true;
}

const Option options[] = {
    toggle(L"enabled", {L"漁り確定ドロップ", L"Guaranteed Loot"}, &enabled, false),
};

}  // namespace

const Tweak lootDropTweak{L"loot_drop", options, 1, install};

}  // namespace tweaks
