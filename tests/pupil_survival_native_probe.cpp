// Installs mod.dll on a copy of the exe mapped into this process, calls the hooked command 120
// with synthetic script states, and runs the hooked pupil marking with a synthetic character.
// The game's own code after each hook needs the running game, so on the copy it is replaced
// by a stub (command 120 returns 0x77, so the test can tell that the game's code ran).
#include <cstdio>
#include <cstring>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t jumpIfChallengers = 0x410220;
constexpr uintptr_t afterCommandPrologue = 0x41022a;  // The 4 instructions MinHook moves
constexpr uintptr_t markPupil = 0x43a1ae;
constexpr uintptr_t afterMarkPupil = 0x43a1b8;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr int gameCodeResult = 0x77;

// pop esi; pop ebx; mov eax,0x77; ret 4
const uint8_t commandStub[] = {0x5e, 0x5b, 0xb8, gameCodeResult, 0x00, 0x00, 0x00, 0xc2, 0x04, 0x00};

struct ScriptState {
    std::vector<uint8_t> state = std::vector<uint8_t>(0x220);
    std::vector<uint8_t> function = std::vector<uint8_t>(0x40);
    ScriptState(uint32_t functionIndex, uint32_t instruction, uint32_t depth) {
        *reinterpret_cast<uint8_t**>(state.data()) = function.data();
        *reinterpret_cast<uint32_t*>(function.data() + 0x24) = functionIndex;
        *reinterpret_cast<uint32_t*>(state.data() + 0x14) = instruction;
        *reinterpret_cast<uint32_t*>(state.data() + 0x20c) = depth;
    }
};

int runCommand(ScriptState& script) {
    const uintptr_t function = va(jumpIfChallengers);
    uint8_t* state = script.state.data();
    int result;
    __asm {
        push 123  // The jump target
        mov ecx, state
        call function
        mov result, eax
    }
    return result;
}

struct Marked {
    float floor;
    uint32_t flags;
    uint32_t eaxAfter;
};

Marked runMarkPupil(float floorFromDefinition) {
    std::vector<uint8_t> character(0x400);
    *reinterpret_cast<float*>(character.data() + 0x328) = floorFromDefinition;
    const uintptr_t function = va(markPupil);
    uint8_t* characterData = character.data();
    uint32_t eaxAfter;
    __asm {
        mov ecx, characterData
        mov eax, 0x5a5a5a5a
        call function
        mov eaxAfter, eax
    }
    return {*reinterpret_cast<float*>(character.data() + 0x328), *reinterpret_cast<uint32_t*>(character.data() + 0x2fc),
        eaxAfter};
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_pupil_survival")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    codeByte(0x43ae4f + 2) = 0x2c;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Pupils Survive Challengers: the pupils' code is not the known version at 0x410220; the tweak "
                     "is skipped") &&
              loggedText("Tweak pupil_survival could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(jumpIfChallengers) && !claimedAt(markPupil) &&
              optionIndex(L"Pupils Survive Challengers") == noItem && codeByte(jumpIfChallengers) == 0x53,
        "nothing is claimed, hooked or shown");
    codeByte(0x43ae4f + 2) = 0x28;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(jumpIfChallengers, 5) && claimed(markPupil, 10), "0x410220 (5 bytes) and 0x43a1ae (10 bytes) are claimed");
    check(codeByte(jumpIfChallengers) == jumpOpcode && codeByte(markPupil) == jumpOpcode, "both hooks are written");
    const uint32_t item = optionIndex(L"Pupils Survive Challengers");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"弟子が死ななくなる",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    std::memcpy(reinterpret_cast<void*>(va(afterCommandPrologue)), commandStub, sizeof(commandStub));
    codeByte(afterMarkPupil) = returnOpcode;
    std::vector<uint8_t> gameState(0x14100);
    gameState[0x1407c] = 1;  // Challengers are in
    *reinterpret_cast<uint8_t**>(va(gameStateGlobal)) = gameState.data();

    ScriptState messengerCheck(3, 1, 0);
    check(runCommand(messengerCheck) == gameCodeResult, "off: the messenger's check runs the game's code");
    check(runMarkPupil(-1.0f).floor == -1.0f, "off: a pupil keeps the health floor of its definition");

    changeOption(item, 1);
    check(runCommand(messengerCheck) == 0, "on: Deshi function 3, instruction 1 sees no challengers");
    ScriptState fightLoop(1, 302, 0), otherFunction(5, 1, 0), nested(3, 1, 1);
    check(runCommand(fightLoop) == gameCodeResult && runCommand(otherFunction) == gameCodeResult &&
              runCommand(nested) == gameCodeResult,
        "on: every other check for challengers (the fight at the dojo, other functions) runs the game's code");
    const Marked marked = runMarkPupil(-1.0f);
    check(marked.floor == 1.0f && (marked.flags & 0x1000) && marked.eaxAfter == 0x5a5a5a5a,
        "on: a pupil appearing while challengers are in gets a health floor of 1, and the game's mark and eax stay");
    gameState[0x1407c] = 0;
    check(runMarkPupil(-1.0f).floor == -1.0f, "on: without challengers a pupil keeps its own floor");

    changeOption(item, 0);
    check(runCommand(messengerCheck) == gameCodeResult, "off again: the messenger's check runs the game's code");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"pupil_survival", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [pupil_survival] enabled=1");

    return finish("pupil_survival_native_probe");
}
