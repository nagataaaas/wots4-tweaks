// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the three places of the My Style screens that read what the player has learned: the bytes
// written and written back, and where the patched jumps go.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t buildCardJump = 0x657fc1;      // jne 0x657ffd (learned: keep the card usable)
constexpr uintptr_t learnedCardPath = 0x657ffd;
constexpr uintptr_t stanceMoveJump = 0x5d0e89;     // je 0x5d0e95 (not learned: next move)
constexpr uintptr_t copyLearnedJump = 0x5ced53;    // jne 0x5cede3 (not learned: skip the move)
constexpr uint8_t jneShort = 0x75;
constexpr uint8_t jeShort = 0x74;
constexpr uint8_t jmpShort = 0xeb;
constexpr uint8_t nop = 0x90;

uintptr_t shortJumpTarget(uintptr_t preferred) {
    return preferred + 2 + *reinterpret_cast<const int8_t*>(va(preferred + 1));
}

bool allNops(uintptr_t preferred, uint32_t length) {
    for (uint32_t offset = 0; offset < length; ++offset)
        if (codeByte(preferred + offset) != nop) return false;
    return true;
}

bool gameCodeIsBack() {
    return codeByte(buildCardJump) == jneShort && shortJumpTarget(buildCardJump) == learnedCardPath &&
           codeByte(stanceMoveJump) == jeShort && codeByte(copyLearnedJump) == 0x0f &&
           codeByte(copyLearnedJump + 1) == 0x85;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_custom_style_all_arts")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(0x5ced45 + 3) = 0x95;  // the record's offset 0x18394 -> 0x18395
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("All My Style Moves: the My Style screens are not the known version at 0x657f4f; the tweak is "
                     "skipped") &&
              loggedText("Tweak custom_style_all_arts could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(buildCardJump) && !claimedAt(stanceMoveJump) && !claimedAt(copyLearnedJump) &&
              optionIndex(L"All My Style Moves") == noItem,
        "nothing is claimed or shown");
    codeByte(0x5ced45 + 3) = 0x94;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(buildCardJump, 2) && claimed(stanceMoveJump, 2) && claimed(copyLearnedJump, 6),
        "the three jumps are claimed (2, 2 and 6 bytes)");
    const uint32_t item = optionIndex(L"All My Style Moves");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"俺流技全解除",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(gameCodeIsBack(), "off: the game's jumps are unchanged");

    changeOption(item, 1);
    check(codeByte(buildCardJump) == jmpShort && shortJumpTarget(buildCardJump) == learnedCardPath,
        "on: every card is built as a learned one (jmp 0x657ffd)");
    check(allNops(stanceMoveJump, 2), "on: choosing a stance never skips a first move as not learned");
    check(allNops(copyLearnedJump, 6), "on: copying a style copies every move");

    changeOption(item, 0);
    check(gameCodeIsBack(), "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"custom_style_all_arts", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [custom_style_all_arts] enabled=1");

    return finish("custom_style_all_arts_native_probe");
}
