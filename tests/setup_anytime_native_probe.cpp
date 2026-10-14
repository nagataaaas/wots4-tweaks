// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the title screen's test of the save's phase after Load: the bytes written and written back, and
// that the code then always reaches the difficulty selection. It also calls the hooked difficulty
// selection with a synthetic title and UI manager; the game's code behind the hook needs the
// running game, so on the copy it is replaced by a stub that records the title it was given.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t phaseZeroJump = 0x67ce07;
constexpr uintptr_t phaseNotZero = 0x67ce0d;          // Where the game goes to continue the save
constexpr uintptr_t chooseDifficulty = 0x67ce09;      // mov byte [ebx+0x3c],1
constexpr uint8_t jneShort = 0x75;
constexpr uint8_t nop = 0x90;
constexpr uintptr_t shortJumpLength = 2;

constexpr uintptr_t difficultySelection = 0x4ed530;
constexpr uintptr_t afterMovedInstructions = 0x4ed536;  // push ebp; mov ebp,esp; and esp,-8
constexpr int32_t stateOpens = -1;
constexpr int32_t stateUpdates = 0;
constexpr uint32_t titleSelectWindowOffset = 0x4c;
constexpr uint32_t titleUiManagerOffset = 0x58;
constexpr uint32_t uiManagerSelectWindowOffset = 0x70;  // [manager+4+0x1b*4]

uint8_t* titleSeenByGame;

// mov [titleSeenByGame],ecx; mov esp,ebp; pop ebp; ret 4
void writeDifficultySelectionStub() {
    uint8_t stub[] = {0x89, 0x0d, 0, 0, 0, 0, 0x8b, 0xe5, 0x5d, 0xc2, 0x04, 0x00};
    const auto recordAddress = reinterpret_cast<uintptr_t>(&titleSeenByGame);
    std::memcpy(stub + 2, &recordAddress, sizeof(recordAddress));
    std::memcpy(reinterpret_cast<void*>(va(afterMovedInstructions)), stub, sizeof(stub));
}

void callDifficultySelection(uint8_t* title, int32_t event) {
    const uintptr_t function = va(difficultySelection);
    __asm {
        push event
        mov ecx, title
        call function
    }
}

void*& field(uint8_t* object, uint32_t offset) { return *reinterpret_cast<void**>(object + offset); }

uintptr_t shortJumpTarget(uintptr_t preferred) {
    return preferred + shortJumpLength + *reinterpret_cast<const int8_t*>(va(preferred + 1));
}

bool gameJumpIsThere() {
    return codeByte(phaseZeroJump) == jneShort && shortJumpTarget(phaseZeroJump) == phaseNotZero;
}

bool runsIntoDifficultySelection() {
    return codeByte(phaseZeroJump) == nop && codeByte(phaseZeroJump + 1) == nop &&
           phaseZeroJump + shortJumpLength == chooseDifficulty;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_setup_anytime")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(phaseZeroJump - 1) = 0x01;  // the phase is compared with 0
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Setup Anytime: the title screen's Load or difficulty selection is not the known version at "
                     "0x67cdf0; the tweak is skipped") &&
              loggedText("Tweak setup_anytime could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(phaseZeroJump) && optionIndex(L"Setup Anytime") == noItem, "nothing is claimed or shown");
    codeByte(phaseZeroJump - 1) = 0x00;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(phaseZeroJump, 2), "the jump is claimed (2 bytes)");
    check(claimed(difficultySelection, 6) && codeByte(difficultySelection) == jumpOpcode,
        "the difficulty selection is hooked and claimed (6 bytes)");
    const uint32_t item = optionIndex(L"Setup Anytime");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"いつでも初期設定",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(gameJumpIsThere(), "off: the game's jne 0x67ce0d is unchanged");

    // The title kept the select window it made; Options then made the one the manager holds now.
    writeDifficultySelectionStub();
    uint8_t title[0x60] = {};
    uint8_t uiManager[0x90] = {};
    uint8_t deletedSelectWindow[0xf0] = {};
    uint8_t currentSelectWindow[0xf0] = {};
    field(title, titleUiManagerOffset) = uiManager;
    field(title, titleSelectWindowOffset) = deletedSelectWindow;
    field(uiManager, uiManagerSelectWindowOffset) = currentSelectWindow;

    callDifficultySelection(title, stateUpdates);
    check(field(title, titleSelectWindowOffset) == deletedSelectWindow && titleSeenByGame == title,
        "an update of the difficulty selection leaves the title's select window as it is");

    titleSeenByGame = nullptr;
    callDifficultySelection(title, stateOpens);
    check(field(title, titleSelectWindowOffset) == currentSelectWindow && titleSeenByGame == title,
        "opening the difficulty selection (even with the option off) uses the UI manager's select window");
    check(loggedText("Setup Anytime: the title's select window had been replaced (by Options)"),
        "the replaced select window is logged");

    field(uiManager, uiManagerSelectWindowOffset) = nullptr;
    callDifficultySelection(title, stateOpens);
    check(field(title, titleSelectWindowOffset) == currentSelectWindow,
        "without a select window in the UI manager the title's is kept");

    changeOption(item, 1);
    check(runsIntoDifficultySelection(), "on: Load always goes on to the difficulty selection (0x67ce09)");

    changeOption(item, 0);
    check(gameJumpIsThere(), "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"setup_anytime", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [setup_anytime] enabled=1");

    return finish("setup_anytime_native_probe");
}
