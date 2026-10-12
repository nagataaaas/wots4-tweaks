// Installs mod.dll on a copy of the exe mapped into this process and runs the smithy's rolls
// (the patched reads of rand and the remainders right after them) with a stand-in rand in the
// copy's import table. The code after each roll needs the running game, so it returns early.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t randImport = 0x9052f8;
constexpr uintptr_t refineRead = 0x61ca17;   // mov ebx,[rand]; ...; idiv edi
constexpr uintptr_t refineRollEnd = 0x61ca27;
constexpr uintptr_t recastRead = 0x61a9fc;   // call [rand]; cdq; mov ecx,6; idiv ecx
constexpr uintptr_t recastRollEnd = 0x61aa0a;
constexpr uintptr_t recastDojimaRead = 0x61aa23;
constexpr uintptr_t recastDojimaEnd = 0x61aa29;
constexpr int standInRoll = 1234;

int __cdecl standInRand() { return standInRoll; }

// Runs from start to end (which returns at once) with eax and edi set; returns edx, the remainder.
uint32_t runRoll(uintptr_t start, uintptr_t end, uint32_t eaxValue, uint32_t ediValue, uint32_t* eaxAfter = nullptr) {
    const uint8_t savedByte = codeByte(end);
    codeByte(end) = returnOpcode;
    const uintptr_t function = va(start);
    uint32_t remainder, eaxResult;
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, function
        mov eax, eaxValue
        mov edi, ediValue
        call ecx
        mov remainder, edx
        mov eaxResult, eax
        pop edi
        pop esi
        pop ebx
    }
    codeByte(end) = savedByte;
    if (eaxAfter) *eaxAfter = eaxResult;
    return remainder;
}

// Refining with quality 6 to 20: low 1, high 20, so rand() % 20.
uint32_t refineRemainder() { return runRoll(refineRead, refineRollEnd, 1, 20); }
uint32_t recastRemainder() { return runRoll(recastRead, recastRollEnd, 0, 0); }
uint32_t recastDojimaRoll() {
    uint32_t roll;
    runRoll(recastDojimaRead, recastDojimaEnd, 0, 0, &roll);
    return roll;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_smith_best_roll")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    // The copy's import table is not bound; give it a rand of our own.
    *reinterpret_cast<uintptr_t*>(va(randImport)) = reinterpret_cast<uintptr_t>(&standInRand);
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is written.
    codeByte(0x61aa04) = 0x07;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Best Smithing: the smithy's refining and recasting are not the known version at 0x61ca17; the "
                     "tweak is skipped") &&
              loggedText("Tweak smith_best_roll could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(refineRead) && optionIndex(L"Best Smithing") == noItem &&
              *reinterpret_cast<uint32_t*>(va(recastRead + 2)) == va(randImport),
        "nothing is claimed, written or shown");
    codeByte(0x61aa04) = 0x06;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(refineRead, 6) && claimed(recastRead, 6) && claimed(recastDojimaRead, 6),
        "the three reads of rand are claimed (6 bytes each)");
    check(*reinterpret_cast<uint32_t*>(va(refineRead + 2)) != va(randImport) && codeByte(refineRead) == 0x8b &&
              codeByte(recastRead) == 0xff && codeByte(recastRead + 1) == 0x15,
        "the reads keep their instructions and read another pointer");
    const uint32_t item = optionIndex(L"Best Smithing");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"鍛冶結果最高",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    check(refineRemainder() == standInRoll % 20 && recastRemainder() == standInRoll % 6 &&
              recastDojimaRoll() == standInRoll,
        "off: the rolls use the game's rand");

    changeOption(item, 1);
    check(refineRemainder() == 19, "on: refining adds the most of rand() % 20 (19)");
    check(runRoll(refineRead, refineRollEnd, 1, 6) == 5 && runRoll(refineRead, refineRollEnd, 1, 1) == 0,
        "on: refining with rand() % 6 and % 1 also gets the most (5 and 0)");
    check(recastRemainder() == 5, "on: recasting adds 10 + 5, the most of rand() % 6");
    check((recastDojimaRoll() & 1) == 1, "on: Dojima's extra roll gets 1 from & 1");

    changeOption(item, 0);
    check(recastRemainder() == standInRoll % 6, "off again: recasting uses the game's rand");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"smith_best_roll", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [smith_best_roll] enabled=1");

    return finish("smith_best_roll_native_probe");
}
