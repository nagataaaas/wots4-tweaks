// Installs mod.dll on a copy of the exe mapped into this process and runs the roll of 0x5acb10
// (the patched call of rand, rand() % 100 and reading the rate from a synthetic reputation row)
// for each choice, with a stand-in rand in the copy's import table. It returns right before the
// comparison, which the test makes itself: challengers come when the roll is below the rate.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t randImport = 0x9052f8;
constexpr uintptr_t appearanceRoll = 0x5acb72;
constexpr uintptr_t rateComparison = 0x5acb84;  // cmp edx,eax; jge
constexpr int standInRoll = 1234;
constexpr int16_t usualRate = 18;
constexpr int16_t lowestReputationRate = 0;
constexpr int16_t highestRate = 20;

int __cdecl standInRand() { return standInRoll; }

struct Roll {
    int roll;
    int rate;
    bool challengersCome() const { return roll < rate; }
};

Roll rollAgainst(int16_t rate) {
    uint8_t reputationRow[0x24] = {};
    *reinterpret_cast<int16_t*>(reputationRow + 0x16) = rate;
    const uint8_t savedByte = codeByte(rateComparison);
    codeByte(rateComparison) = returnOpcode;
    const uintptr_t function = va(appearanceRoll);
    uint8_t* row = reputationRow;
    int remainder, rateRead;
    __asm {
        push esi
        mov esi, row
        call function
        mov remainder, edx
        mov rateRead, eax
        pop esi
    }
    codeByte(rateComparison) = savedByte;
    return {remainder, rateRead};
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_dojo_raid_appearance")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    *reinterpret_cast<uintptr_t*>(va(randImport)) = reinterpret_cast<uintptr_t>(&standInRand);
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is written.
    codeByte(0x5acb83) = 0x18;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Challenger Appearance: the roll for challengers is not the known version at 0x5acb72; the "
                     "tweak is skipped") &&
              loggedText("Tweak dojo_raid_appearance could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(appearanceRoll) && optionIndex(L"Challenger Appearance") == noItem &&
              *reinterpret_cast<uint32_t*>(va(appearanceRoll + 2)) == va(randImport),
        "nothing is claimed, written or shown");
    codeByte(0x5acb83) = 0x16;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(appearanceRoll, 6) && *reinterpret_cast<uint32_t*>(va(appearanceRoll + 2)) != va(randImport),
        "the call of rand at 0x5acb72 is claimed (6 bytes) and reads another pointer");
    const uint32_t item = optionIndex(L"Challenger Appearance");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"道場破り出現率" &&
              std::wstring(optionItem(item).choices[1].japanese) == L"確定出現" &&
              std::wstring(optionItem(item).choices[2].english) == L"Never",
        "one option with the choices default / always / never is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the game decides");

    const Roll gameRoll = rollAgainst(usualRate);
    check(gameRoll.roll == standInRoll % 100 && gameRoll.rate == usualRate,
        "default: the game's rand() % 100 against the row's rate (+0x16)");
    changeOption(item, 1);
    check(rollAgainst(usualRate).challengersCome() && rollAgainst(1).challengersCome(),
        "always: challengers come at any rate above 0");
    check(!rollAgainst(lowestReputationRate).challengersCome(), "always: none come at the lowest reputation (rate 0)");
    changeOption(item, 2);
    check(!rollAgainst(usualRate).challengersCome() && !rollAgainst(highestRate).challengersCome(),
        "never: no challengers come, even at the highest rate");
    changeOption(item, 0);
    check(rollAgainst(usualRate).roll == standInRoll % 100, "default again: the game's rand");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"dojo_raid_appearance", L"appearance", 99, configPath().c_str()) == 1,
        "the options save writes [dojo_raid_appearance] appearance=1");

    return finish("dojo_raid_appearance_native_probe");
}
