// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and
// checks the four ability point checks of the My Style screens: the bytes written and written
// back, where the jumps go, and the budget the patched code computes from synthetic shared data.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t chooseOneJump = 0x5cb742;
constexpr uintptr_t chooseAtOnceBudget = 0x5cbad1;
constexpr uintptr_t copyStyleBudget = 0x5ceb35;
constexpr uintptr_t confirmJump = 0x5d3b30;
constexpr uint32_t budgetLength = 10;
constexpr uintptr_t chooseOneAccepted = 0x5cb832;  // Where the game goes when the points suffice
constexpr uintptr_t confirmAccepted = 0x5d3b39;

const uint8_t gameBudget[] = {0x0f, 0xbf, 0x41, 0x52, 0x0f, 0xbf, 0x49, 0x50, 0x2b, 0xc1};

bool codeIs(uintptr_t preferred, const uint8_t* bytes, size_t count) {
    return std::memcmp(reinterpret_cast<const void*>(va(preferred)), bytes, count) == 0;
}

uintptr_t jumpTarget(uintptr_t preferred) {
    const uint8_t opcode = codeByte(preferred);
    if (opcode == 0xe9) return preferred + 5 + *reinterpret_cast<const int32_t*>(va(preferred + 1));
    if (opcode == 0xeb) return preferred + 2 + *reinterpret_cast<const int8_t*>(va(preferred + 1));
    return 0;
}

struct Budget {
    int32_t budget;  // eax
    int32_t total;   // ecx
};

// Runs the 10 bytes at preferred with ecx = shared data whose +0x52 holds the points and +0x50
// the chosen moves' total; the byte after them returns.
Budget runBudget(uintptr_t preferred, int16_t points, int16_t total) {
    uint8_t shared[0x60] = {};
    *reinterpret_cast<int16_t*>(shared + 0x52) = points;
    *reinterpret_cast<int16_t*>(shared + 0x50) = total;
    const uint8_t savedByte = codeByte(preferred + budgetLength);
    codeByte(preferred + budgetLength) = returnOpcode;
    const uintptr_t function = va(preferred);
    uint8_t* sharedData = shared;
    int32_t budgetAfter, totalAfter;
    __asm {
        mov ecx, sharedData
        call function
        mov budgetAfter, eax
        mov totalAfter, ecx
    }
    codeByte(preferred + budgetLength) = savedByte;
    return {budgetAfter, totalAfter};
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_custom_style_limit")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(confirmJump + 1) = 0x08;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Unlimited My Style: the My Style screens are not the known version at 0x5cb734; the tweak is "
                     "skipped") &&
              loggedText("Tweak custom_style_limit could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(chooseOneJump) && optionIndex(L"Unlimited My Style") == noItem, "nothing is claimed or shown");
    codeByte(confirmJump + 1) = 0x07;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(chooseOneJump, 6) && claimed(chooseAtOnceBudget, budgetLength) &&
              claimed(copyStyleBudget, budgetLength) && claimed(confirmJump, 2),
        "the four checks are claimed (6, 10, 10 and 2 bytes)");
    const uint32_t item = optionIndex(L"Unlimited My Style");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"俺流上限突破",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(codeByte(chooseOneJump) == 0x0f && codeIs(chooseAtOnceBudget, gameBudget, budgetLength) &&
              codeIs(copyStyleBudget, gameBudget, budgetLength) && codeByte(confirmJump) == 0x7d,
        "off: the game's code is unchanged");
    check(runBudget(chooseAtOnceBudget, 300, 120).budget == 180, "off: choosing at once has points - total = 180");

    changeOption(item, 1);
    check(jumpTarget(chooseOneJump) == chooseOneAccepted && codeByte(chooseOneJump + 5) == 0x90,
        "on: choosing one move always goes where enough points go (0x5cb832)");
    check(jumpTarget(confirmJump) == confirmAccepted, "on: confirming always goes where enough points go (0x5d3b39)");
    const Budget atOnce = runBudget(chooseAtOnceBudget, 300, 120);
    check(atOnce.budget == 0x7fffffff && atOnce.total == 120,
        "on: choosing at once gets the largest budget and keeps the total in ecx as the game does");
    const Budget copying = runBudget(copyStyleBudget, 0, 25590);
    check(copying.budget == 0x7fffffff && copying.total == 25590, "on: copying a style gets the largest budget");

    changeOption(item, 0);
    check(codeByte(chooseOneJump) == 0x0f && codeIs(chooseAtOnceBudget, gameBudget, budgetLength) &&
              codeIs(copyStyleBudget, gameBudget, budgetLength) && codeByte(confirmJump) == 0x7d &&
              runBudget(copyStyleBudget, 300, 120).budget == 180,
        "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"custom_style_limit", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [custom_style_limit] enabled=1");

    return finish("custom_style_limit_native_probe");
}
