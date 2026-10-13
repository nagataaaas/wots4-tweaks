// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the refining check of the weapon's maximum quality: the bytes written and written back, and
// where the jump goes.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t maxQualityJump = 0x61bb64;
constexpr uintptr_t refineAccepted = 0x61bb94;  // Where the game goes when quality < maximum
constexpr uint8_t jlShort = 0x7c;
constexpr uint8_t jmpShort = 0xeb;
constexpr uintptr_t shortJumpLength = 2;

uintptr_t shortJumpTarget(uintptr_t preferred) {
    return preferred + shortJumpLength + *reinterpret_cast<const int8_t*>(va(preferred + 1));
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_refine_quality_limit")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(maxQualityJump + 3) = 0x18;  // the message number
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Unlimited Refining: the smithy's refining is not the known version at 0x61bb56; the tweak is "
                     "skipped") &&
              loggedText("Tweak refine_quality_limit could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(maxQualityJump) && optionIndex(L"Unlimited Refining") == noItem, "nothing is claimed or shown");
    codeByte(maxQualityJump + 3) = 0x17;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(maxQualityJump, 2), "the jump is claimed (2 bytes)");
    const uint32_t item = optionIndex(L"Unlimited Refining");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"質上限突破",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(codeByte(maxQualityJump) == jlShort && shortJumpTarget(maxQualityJump) == refineAccepted,
        "off: the game's jl 0x61bb94 is unchanged");

    changeOption(item, 1);
    check(codeByte(maxQualityJump) == jmpShort && shortJumpTarget(maxQualityJump) == refineAccepted,
        "on: refining always goes where quality below the maximum goes (0x61bb94)");

    changeOption(item, 0);
    check(codeByte(maxQualityJump) == jlShort && shortJumpTarget(maxQualityJump) == refineAccepted,
        "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"refine_quality_limit", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [refine_quality_limit] enabled=1");

    return finish("refine_quality_limit_native_probe");
}
