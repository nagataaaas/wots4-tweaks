// Installs mod.dll on a copy of the exe mapped into this process and starts night visits:
// calls the hooked 0x69f280 with synthetic game objects behind the game's globals. Building
// the partners needs the running game, so on the copy the code right after the instruction
// MinHook moves returns at once. Writes config.ini only in a folder under %TEMP%; never
// attaches to the game process or reads saves.
#include <cstdio>
#include <string>
#include "mapped_exe.h"
#include "synthetic_definitions.h"

using namespace mapped_exe;
using namespace synthetic;

namespace {

constexpr uintptr_t createPartners = 0x69f280;
constexpr uintptr_t afterMovedInstruction = 0x69f285;
constexpr uintptr_t findDefinition = 0x43c250;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uintptr_t yobaiManagerGlobal = 0x9ecbb4;
constexpr uintptr_t definitionRootGlobal = 0x9e9640;
constexpr uintptr_t promiseByCommandSpecialNumber = 0x69aa75;  // The 0x13 of cmp word [eax+6],0x13

template <typename T>
void setGlobal(uintptr_t global, T value) {
    *reinterpret_cast<T*>(va(global)) = value;
}

// The game's own lookup (eax = table holder, ecx = ID), to check the synthetic layout.
const uint8_t* findWithGameCode(const uint8_t* tableHolder, int32_t id) {
    const uintptr_t function = va(findDefinition);
    const uint8_t* found;
    __asm {
        mov eax, tableHolder
        mov ecx, id
        call function
        mov found, eax
    }
    return found;
}

struct Registers {
    uint32_t ebxValue, esiValue, ediValue;
};

// Calls 0x69f280 as 0x4eb45a does (edi = the night visit progress) and returns the registers after it.
Registers startNightVisit() {
    const uintptr_t function = va(createPartners);
    uint32_t ebxAfter, esiAfter, ediAfter;
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, 0x11111111
        mov esi, 0x22222222
        mov edi, 0x33333333
        call function
        mov ebxAfter, ebx
        mov esiAfter, esi
        mov ediAfter, edi
        pop edi
        pop esi
        pop ebx
    }
    return {ebxAfter, esiAfter, ediAfter};
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_laura_yobai")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }

    GameObjects objects;
    const uint8_t* rootPointer = objects.definitionRoot.data();
    setGlobal(gameStateGlobal, objects.gameState.data());
    setGlobal(yobaiManagerGlobal, objects.manager.data());
    setGlobal(definitionRootGlobal, &rootPointer);

    check(findWithGameCode(objects.tableHolder, lauraDefinition) ==
                  findPersonDefinition(objects.definitionRoot.data(), lauraDefinition) &&
              findWithGameCode(objects.tableHolder, missingDefinition) == nullptr &&
              findWithGameCode(objects.tableHolder, definitionCount) == nullptr,
        "the game's lookup (0x43c250) finds the same definitions as the tweak");

    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked. The other tweaks install
    // here, and fail to hook again on the next initialization; only this tweak is checked.
    auto* specialNumber = reinterpret_cast<uint8_t*>(va(promiseByCommandSpecialNumber));
    *specialNumber = 0x14;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Laura night visit partner: the game code at 0x69f280 is not the known version") &&
              loggedText("Tweak laura_yobai could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(createPartners) && optionIndex(L"Laura's Night Crawling") == noItem &&
              codeByte(createPartners) == 0xa1,
        "nothing is claimed, hooked or shown");
    *specialNumber = 0x13;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(createPartners, 5), "0x69f280 is claimed (5 bytes)");
    check(codeByte(createPartners) == jumpOpcode, "the hook is written at 0x69f280");
    const uint32_t item = optionIndex(L"Laura's Night Crawling");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"ローラの夜這い" &&
              std::wstring(optionItem(item).choices[0].english) == L"Don't Fix" &&
              std::wstring(optionItem(item).choices[1].japanese) == L"偽物" &&
              std::wstring(optionItem(item).choices[2].english) == L"Real",
        "one option with the choices not fixed / fake / real is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == partnerAsTheGameDecides, "without config.ini the option is not fixed");

    codeByte(afterMovedInstruction) = returnOpcode;

    objects.promise(otherDefinition);
    objects.mark() = markSet;
    startNightVisit();
    check(objects.mark() == markSet, "not fixed: a stale mark is left as the game has it");

    changeOption(item, partnerFake);
    objects.promise(lauraDefinition);
    objects.mark() = markUnset;
    const Registers after = startNightVisit();
    check(objects.mark() == markSet, "fake: Laura's promise after a restart starts with the mark set");
    check(loggedText("Night visit with person definition 454: Laura mark changed from 0 to 1 "
                     "(partner fixed to fake (sailor))"),
        "the changed mark is logged");
    check(after.ebxValue == 0x11111111 && after.esiValue == 0x22222222 && after.ediValue == 0x33333333,
        "ebx, esi and edi (the progress) are kept for the game's code");
    objects.promise(otherDefinition);
    startNightVisit();
    check(objects.mark() == markUnset, "fake: another partner's promise clears a stale mark");

    changeOption(item, partnerReal);
    objects.promise(lauraDefinition);
    objects.mark() = markSet;
    startNightVisit();
    check(objects.mark() == markUnset, "real: Laura's promise made in this session starts with the mark cleared");

    const size_t loggedLength = logged.size();
    objects.promise(noPromise);
    objects.mark() = markSet;
    startNightVisit();
    check(objects.mark() == markSet && logged.size() == loggedLength, "no promise: nothing changes or is logged");
    setGlobal(yobaiManagerGlobal, static_cast<uint8_t*>(nullptr));
    startNightVisit();
    check(true, "no night visit manager: the visit starts without a crash");
    setGlobal(yobaiManagerGlobal, objects.manager.data());

    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"laura_yobai", L"partner", 99, configPath().c_str()) == partnerReal,
        "the options save writes [laura_yobai] partner=2");

    return finish("laura_yobai_native_probe");
}
