// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the character setup's two checks of owned clothes: the bytes written and written back, and that
// a patched call leaves al = 1 and the stack as the call's ret 4 would.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t partCheckCall = 0x47591a;  // call 0x4db260, after push ebx
constexpr uintptr_t setCheckCall = 0x479dd5;   // call 0x4db260, after push eax
constexpr uintptr_t partUnlockCall = 0x475929;  // All Unlocks' patch in the same function
constexpr uintptr_t shopClothesCall = 0x624236;  // The clothes shop's check, left as it is

bool codeIs(uintptr_t preferred, std::initializer_list<uint8_t> bytes) {
    return std::memcmp(reinterpret_cast<const void*>(va(preferred)), bytes.begin(), bytes.size()) == 0;
}

// Runs the 5 bytes at preferred after a push, as the game does; returns al, and whether esp came
// back to where it was before the push.
uint8_t runPatchedCall(uintptr_t preferred, bool* stackKept) {
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    stub[0] = 0x50;  // push eax
    std::memcpy(stub + 1, reinterpret_cast<const void*>(va(preferred)), 5);
    stub[6] = returnOpcode;
    uint8_t result;
    uint32_t espBefore, espAfter;
    __asm {
        mov espBefore, esp
        xor eax, eax
        call stub
        mov result, al
        mov espAfter, esp
    }
    VirtualFree(stub, 0, MEM_RELEASE);
    *stackKept = espBefore == espAfter;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_appearance_all")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(setCheckCall + 6) = 0x85;  // test al,al -> test ch,al
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("All Appearances: the character setup's check of owned clothes is not the known version at "
                     "0x47591a; the tweak is skipped") &&
              loggedText("Tweak appearance_all could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(partCheckCall) && !claimedAt(setCheckCall) && optionIndex(L"All Appearances") == noItem,
        "nothing is claimed or shown");
    codeByte(setCheckCall + 6) = 0xc0;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(partCheckCall, 5) && claimed(setCheckCall, 5), "the two calls are claimed (5 bytes each)");
    check(claimedAt(partUnlockCall) && !claimedAt(shopClothesCall),
        "All Unlocks' patch in the same function is claimed separately, and the clothes shop is not claimed");
    const uint32_t item = optionIndex(L"All Appearances");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"全容姿使用可",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(codeIs(partCheckCall, {0xe8, 0x41, 0x59, 0x06, 0x00}) && codeIs(setCheckCall, {0xe8, 0x86, 0x14, 0x06, 0x00}),
        "off: the game's calls of the clothes check are unchanged");

    changeOption(item, 1);
    bool stackKept = false;
    check(runPatchedCall(partCheckCall, &stackKept) == 1 && stackKept,
        "on: a part's check gives owned (al = 1) and removes the pushed game state");
    check(runPatchedCall(setCheckCall, &stackKept) == 1 && stackKept,
        "on: a character set's check gives owned and removes the pushed game state");

    changeOption(item, 0);
    check(codeIs(partCheckCall, {0xe8, 0x41, 0x59, 0x06, 0x00}) && codeIs(setCheckCall, {0xe8, 0x86, 0x14, 0x06, 0x00}),
        "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"appearance_all", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [appearance_all] enabled=1");

    return finish("appearance_all_native_probe");
}
