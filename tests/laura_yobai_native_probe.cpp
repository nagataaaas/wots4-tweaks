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
constexpr uint8_t returnOpcode = 0xc3;

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

std::wstring testDirectory;

bool createTestDirectory() {
    wchar_t temporary[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temporary)) return false;
    testDirectory = std::wstring(temporary) + L"wots4_laura_yobai_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    return CreateDirectoryW(testDirectory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring configPath() { return testDirectory + L"config.ini"; }

void removeTestDirectory() {
    DeleteFileW(configPath().c_str());
    RemoveDirectoryW(testDirectory.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory()) {
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

    HMODULE mod = LoadLibraryA(argv[2]);
    auto initialize = mod ? reinterpret_cast<wots4::Initialize>(GetProcAddress(mod, "Wots4ModInitialize")) : nullptr;
    check(initialize != nullptr, "mod.dll exports Wots4ModInitialize");
    if (!initialize) return 1;
    const wots4::ModContext context{sizeof(context), wots4::modApiVersion, nullptr, mod,
        reinterpret_cast<HMODULE>(imageBase), testDirectory.c_str(), &host};

    // A different game: the tweak is skipped and nothing is hooked.
    auto* specialNumber = reinterpret_cast<uint8_t*>(va(promiseByCommandSpecialNumber));
    *specialNumber = 0x14;
    check(initialize(&context) == TRUE, "with changed game code the Mod still initializes");
    check(loggedText("Laura night visit partner: the game code at 0x69f280 is not the known version") &&
              loggedText("Tweak laura_yobai could not be installed"),
        "the skipped tweak is logged");
    check(claims.empty() && pages.empty() && *reinterpret_cast<uint8_t*>(va(createPartners)) == 0xa1,
        "nothing is claimed, hooked or shown");
    *specialNumber = 0x13;

    check(initialize(&context) == TRUE, "the Mod initializes on the known game code");
    check(claimed(createPartners, 5) && claims.size() == 1, "0x69f280 is claimed (5 bytes)");
    check(*reinterpret_cast<uint8_t*>(va(createPartners)) == 0xe9, "the hook is written at 0x69f280");
    check(pages.size() == 1 && pages[0].items.size() == 1 && pages[0].items[0].kind == wots4::optionChoice &&
              pages[0].items[0].choiceCount == 3 &&
              std::wstring(pages[0].items[0].label) == L"ローラの夜這い相手を固定する" &&
              std::wstring(pages[0].items[0].choices[1]) == L"偽物",
        "one option with the choices not fixed / fake / real is shown");
    if (pages.size() != 1) return 1;
    const wots4::OptionsPageCallbacks& option = pages[0].callbacks;
    uint32_t shownValue = 99;
    option.read(option.context, &shownValue, 1);
    check(shownValue == partnerAsTheGameDecides, "without config.ini the option is not fixed");

    *reinterpret_cast<uint8_t*>(va(afterMovedInstruction)) = returnOpcode;

    objects.promise(otherDefinition);
    objects.mark() = markSet;
    startNightVisit();
    check(objects.mark() == markSet, "not fixed: a stale mark is left as the game has it");

    option.changed(option.context, 0, partnerFake);
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

    option.changed(option.context, 0, partnerReal);
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

    removeTestDirectory();
    std::printf("%d checks passed, %d failed. Mapped exe copy in this process only, config.ini only under "
                "%%TEMP%%; no game installation, saves, or game process were written.\n",
        passed, failed);
    return failed ? 1 : 0;
}
