// While dojo challengers are in (game state +0x1407c), the pupil who brings the news kills
// pupils in the pupils' action script Deshi; the exe itself never removes a pupil for the
// challengers. Deshi function 3 (talking to the pupil) starts with command 120 "jump to 123 if
// challengers are in", and that branch kills a random pupil (command 127), or with one pupil
// left reports the dojo lost and kills the messenger (command 125). The hook makes that
// command 120 see no challengers, so the talk is an ordinary one. Pupils who fall in the
// fight at the dojo die as in the game (user decision 2026-10-13).
#include "pupil_survival.h"
#include "../game_code.h"

namespace tweaks {

namespace {

// Command 120, __thiscall (script state, argument), ret 4. Returns 0 either way; with
// challengers in it moves the script to the argument's instruction first.
constexpr uintptr_t jumpIfChallengers = 0x410220;
constexpr uint32_t jumpIfChallengersHookLength = 5;  // push ebx; push esi; mov esi,ecx; mov eax,[esi+0x20c]
constexpr uintptr_t jumpIfChallengersEntry = 0x40aec8;  // mov [0x9ecda0],0x410220 (the command table)
constexpr uintptr_t commandTableSlot = 0x9ecda0;

// The script state of command 120
constexpr uint32_t callDepthOffset = 0x20c;      // 0 = running a function itself
constexpr uint32_t instructionIndexOffset = 0x14;  // Of the depth 0 frame (+0xc, then +8)
constexpr uint32_t functionIndexOffset = 0x24;   // In [state], at depth 0
// Deshi function 3, instruction 1 is the only command 120 there in any action script.
constexpr uint32_t talkedToByMessengerFunction = 3;
constexpr uint32_t challengersCheckInstruction = 1;

using JumpIfChallengers = int(__fastcall*)(uint8_t* state, void* unused, int argument);

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Pupils Survive Challengers");
JumpIfChallengers originalJumpIfChallengers;

template <typename T>
T readAt(const uint8_t* address) {
    return *reinterpret_cast<const T*>(address);
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t commandPrologue[] = {0x53, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x0c, 0x02, 0x00, 0x00};
    const uint8_t storeCommand[] = {0xc7, 0x05};
    return game.codeIs(jumpIfChallengers, commandPrologue, sizeof(commandPrologue)) &&
           game.codeIs(jumpIfChallengersEntry, storeCommand, sizeof(storeCommand)) &&
           game.operandIs(jumpIfChallengersEntry + 2, commandTableSlot) &&
           game.operandIs(jumpIfChallengersEntry + 6, jumpIfChallengers);
}

bool isMessengerCheck(const uint8_t* state) {
    if (readAt<uint32_t>(state + callDepthOffset) != 0) return false;
    if (readAt<uint32_t>(state + instructionIndexOffset) != challengersCheckInstruction) return false;
    const auto* function = readAt<const uint8_t*>(state);
    return function && readAt<uint32_t>(function + functionIndexOffset) == talkedToByMessengerFunction;
}

// Runs each time an action script checks for challengers; reads four values at most.
int __fastcall jumpIfChallengersHook(uint8_t* state, void* unused, int argument) {
    if (enabled == toggleOn && isMessengerCheck(state)) return 0;  // As with no challengers
    return originalJumpIfChallengers(state, unused, argument);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the pupils' code is not the known version", jumpIfChallengers);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, jumpIfChallengers, jumpIfChallengersHookLength,
            reinterpret_cast<void*>(&jumpIfChallengersHook), reinterpret_cast<void**>(&originalJumpIfChallengers))) {
        logger.installFailed(error, jumpIfChallengers);
        return false;
    }
    return true;
}

// 弟子 is "pupils" and 道場破り "challenger" in the game's messages.
const Option options[] = {
    toggle(L"enabled", {L"弟子が死ななくなる", L"Pupils Survive Challengers"}, &enabled, false),
};

}  // namespace

const Tweak pupilSurvivalTweak{L"pupil_survival", options, 1, install};

}  // namespace tweaks
