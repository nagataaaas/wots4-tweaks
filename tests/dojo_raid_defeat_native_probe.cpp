// Installs mod.dll on a copy of the exe mapped into this process and runs the roll of 0x5acbb0
// (the patched call of rand and rand() % 100) for each choice, with a stand-in rand in the
// copy's import table. The checks after the roll need the running game, so it returns there.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t randImport = 0x9052f8;
constexpr uintptr_t defeatRoll = 0x5acbb5;
constexpr uintptr_t afterRemainder = 0x5acbc3;
constexpr int standInRoll = 1234;
constexpr int senseiRepelBelow = 80;
constexpr int highestPupilRate = 20;

int __cdecl standInRand() { return standInRoll; }

int rollRemainder() {
    const uint8_t savedByte = codeByte(afterRemainder);
    codeByte(afterRemainder) = returnOpcode;
    const uintptr_t function = va(defeatRoll);
    int remainder;
    __asm {
        call function
        mov remainder, edx
    }
    codeByte(afterRemainder) = savedByte;
    return remainder;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_dojo_raid_defeat")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    *reinterpret_cast<uintptr_t*>(va(randImport)) = reinterpret_cast<uintptr_t>(&standInRand);
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is written.
    codeByte(0x5acc0f) = 0x51;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Repelling Challengers: the check for repelling challengers is not the known version at "
                     "0x5acbb5; the tweak is skipped") &&
              loggedText("Tweak dojo_raid_defeat could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(defeatRoll) && optionIndex(L"Repelling Challengers") == noItem &&
              *reinterpret_cast<uint32_t*>(va(defeatRoll + 2)) == va(randImport),
        "nothing is claimed, written or shown");
    codeByte(0x5acc0f) = 0x50;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(defeatRoll, 6) && *reinterpret_cast<uint32_t*>(va(defeatRoll + 2)) != va(randImport),
        "the call of rand at 0x5acbb5 is claimed (6 bytes) and reads another pointer");
    const uint32_t item = optionIndex(L"Repelling Challengers");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"道場破り撃破" &&
              std::wstring(optionItem(item).choices[1].japanese) == L"自動撃破" &&
              std::wstring(optionItem(item).choices[2].english) == L"Never",
        "one option with the choices default / always / never is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the game decides");

    check(rollRemainder() == standInRoll % 100, "default: the roll is the game's rand() % 100");
    changeOption(item, 1);
    const int always = rollRemainder();
    check(always < senseiRepelBelow && always < 0, "always: the roll is below the sensei's 80 and any pupils' rate, even 0");
    changeOption(item, 2);
    const int never = rollRemainder();
    check(never >= senseiRepelBelow && never >= highestPupilRate, "never: the roll is at or above 80 and every rate");
    changeOption(item, 0);
    check(rollRemainder() == standInRoll % 100, "default again: the game's rand");

    changeOption(item, 2);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"dojo_raid_defeat", L"defeat", 99, configPath().c_str()) == 2,
        "the options save writes [dojo_raid_defeat] defeat=2");

    return finish("dojo_raid_defeat_native_probe");
}
