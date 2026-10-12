// While dojo challengers are in (game state +0x1407c), pupils die in three ways, all in the
// pupils' action script Deshi; the exe itself never removes a pupil for the challengers.
// - Talking to the pupil who brings the news: Deshi function 3 starts with command 120 "jump to
//   123 if challengers are in", and that branch kills a random pupil (command 127), or with
//   one pupil left reports the dojo lost and kills the messenger (command 125).
// - Falling in the fight at the dojo, which Deshi function 13 records as a death.
// The first hook makes that command 120 see no challengers, so the talk is an ordinary one.
// The second gives every pupil who appears while challengers are in a health floor of 1, as
// the main characters have, so blows stop at 1 and the pupil never falls.
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
// or dword [ecx+0x2fc],0x1000 in 0x43a160: marks a character that appears as a pupil, after
// 0x43aa70 created it (and set its health floor from its definition at 0x43ae4f).
constexpr uintptr_t markPupil = 0x43a1ae;
constexpr uint32_t markPupilLength = 10;
constexpr uintptr_t gameStateGlobal = 0x9e967c;

// The script state of command 120
constexpr uint32_t callDepthOffset = 0x20c;      // 0 = running a function itself
constexpr uint32_t instructionIndexOffset = 0x14;  // Of the depth 0 frame (+0xc, then +8)
constexpr uint32_t functionIndexOffset = 0x24;   // In [state], at depth 0
// Deshi function 3, instruction 1 is the only command 120 there in any action script.
constexpr uint32_t talkedToByMessengerFunction = 3;
constexpr uint32_t challengersCheckInstruction = 1;

constexpr uint32_t challengersInOffset = 0x1407c;  // uint8 in the game state
constexpr uint32_t healthFloorOffset = 0x328;      // float; blows stop there (0x49dcff, 0x486b69)
constexpr float survivingHealthFloor = 1.0f;

using JumpIfChallengers = int(__fastcall*)(uint8_t* state, void* unused, int argument);

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Pupils Survive Challengers");
JumpIfChallengers originalJumpIfChallengers;
void* originalMarkPupil;

template <typename T>
T readAt(const uint8_t* address) {
    return *reinterpret_cast<const T*>(address);
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t commandPrologue[] = {0x53, 0x56, 0x8b, 0xf1, 0x8b, 0x86, 0x0c, 0x02, 0x00, 0x00};
    const uint8_t storeCommand[] = {0xc7, 0x05};
    // mov esi,[edi+8]; movsx ebx,byte [eax+0x39]; call 0x465590; test eax,eax; je;
    // mov ecx,[eax+0xc]; or dword [ecx+0x2fc],0x1000; mov eax,[eax+0xc]; and dword [eax+0x2fc],...
    const uint8_t pupilAppears[] = {0x8b, 0x77, 0x08, 0x0f, 0xbe, 0x58, 0x39, 0xe8, 0xe9, 0xb3, 0x02, 0x00, 0x85,
        0xc0, 0x74, 0x3d, 0x8b, 0x48, 0x0c, 0x81, 0x89, 0xfc, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x8b, 0x40,
        0x0c, 0x81, 0xa0, 0xfc, 0x02, 0x00, 0x00};
    // The health floor from the definition: movsx eax,word [ebx+0x2e] ... fstp dword [esi+0x328]
    const uint8_t floorFromDefinition[] = {0x0f, 0xbf, 0x43, 0x2e};
    const uint8_t storeFloor[] = {0xd9, 0x9e, 0x28, 0x03, 0x00, 0x00};
    // The same "challengers in" byte: cmp byte [edx+0x1407c],0 after mov edx,[0x9e967c]
    const uint8_t challengersIn[] = {0x80, 0xba, 0x7c, 0x40, 0x01, 0x00, 0x00};
    return game.codeIs(jumpIfChallengers, commandPrologue, sizeof(commandPrologue)) &&
           game.codeIs(jumpIfChallengersEntry, storeCommand, sizeof(storeCommand)) &&
           game.operandIs(jumpIfChallengersEntry + 2, commandTableSlot) &&
           game.operandIs(jumpIfChallengersEntry + 6, jumpIfChallengers) &&
           game.codeIs(0x43a19b, pupilAppears, sizeof(pupilAppears)) &&
           game.codeIs(0x43ae3f, floorFromDefinition, sizeof(floorFromDefinition)) &&
           game.codeIs(0x43ae4f, storeFloor, sizeof(storeFloor)) && game.operandIs(0x43adf0, gameStateGlobal) &&
           game.codeIs(0x43adf4, challengersIn, sizeof(challengersIn));
}

bool challengersAreIn() {
    const auto* gameState = reinterpret_cast<const uint8_t*>(game.readUint32(gameStateGlobal));
    return gameState && gameState[challengersInOffset] != 0;
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

void __cdecl keepPupilStanding(uint8_t* character) {
    if (enabled != toggleOn || !challengersAreIn()) return;
    *reinterpret_cast<float*>(character + healthFloorOffset) = survivingHealthFloor;
}

// ecx = the character; eax is used right after, so every register is kept.
__declspec(naked) void markPupilHook() {
    __asm {
        pushad
        push ecx
        call keepPupilStanding
        add esp, 4
        popad
        jmp dword ptr [originalMarkPupil]
    }
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
    if (CodeChangeError error = hookGameCode(context, game, markPupil, markPupilLength,
            reinterpret_cast<void*>(&markPupilHook), &originalMarkPupil)) {
        unhookGameCode(game, jumpIfChallengers);
        logger.installFailed(error, markPupil);
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
