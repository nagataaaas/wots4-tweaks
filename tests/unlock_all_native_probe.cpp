// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the unlock checks: the bytes written and written back, and the flags a patched check leaves
// when run.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t hardDifficultyCheck = 0x4ed74e;  // cmp byte [edi+0x43334],2; jne
constexpr uintptr_t bodyTypeCheck = 0x5f4eb5;        // cmp byte [ebx+0x43340],2
constexpr uintptr_t skinColorCheck = 0x4755f4;       // cmp byte [esi+ecx+0x43330],2
constexpr uintptr_t dualSwordDrawCheck = 0x499c65;   // cmp byte [esi+0x43396],bl
constexpr uintptr_t faceCheckCall = 0x475929;        // call 0x4dba10
constexpr uintptr_t riflePurchaseCheck = 0x6267a0;
constexpr uintptr_t unlocksScreenCall = 0x633b6e;    // The Unlocks screen's call, left as it is
const uintptr_t allChecks[] = {0x4ed74e, 0x4ed7cc, 0x5f4eb5, 0x5f4faf, 0x4755f4, 0x4756d4, 0x475929, 0x479df6,
    0x4626e1, 0x462713, 0x463850, 0x463882, 0x499c65, 0x499c96, 0x496fa9, 0x496ffb, 0x497213, 0x49724e, 0x4975ca,
    0x497607, 0x499838, 0x499871, 0x4979f5, 0x497a27, 0x593a5f, 0x593a8e, 0x593aba, 0x593ae6, 0x5d0a16, 0x5d0a54,
    0x5d0aa4, 0x5d0ae2, 0x5d1697, 0x5d16d4, 0x5d1722, 0x5d175f, 0x620428, 0x62045a, 0x62676e, 0x6267a0, 0x6275ce,
    0x6710b1, 0x6710dd};
constexpr uintptr_t enemyDropCheck = 0x4f5987;  // Which weapons enemies drop, left as it is
constexpr uint32_t gameStateUnlocks = 0x43330;
constexpr uint32_t hardDifficulty = 4;

const uint8_t hardDifficultyOriginal[] = {0x80, 0xbf, 0x34, 0x33, 0x04, 0x00, 0x02};

bool codeIs(uintptr_t preferred, std::initializer_list<uint8_t> bytes) {
    return std::memcmp(reinterpret_cast<const void*>(va(preferred)), bytes.begin(), bytes.size()) == 0;
}

// Runs the 7 bytes at hardDifficultyCheck with edi = gameState and returns whether ZF was set,
// that is whether the game takes the difficulty as bought.
bool runHardDifficultyCheck(const uint8_t* gameState) {
    uint8_t* stub = static_cast<uint8_t*>(VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    std::memcpy(stub, reinterpret_cast<const void*>(va(hardDifficultyCheck)), 7);
    const uint8_t setZeroFlag[] = {0x0f, 0x94, 0xc0, returnOpcode};  // sete al; ret
    std::memcpy(stub + 7, setZeroFlag, sizeof(setZeroFlag));
    uint8_t zeroFlag;
    __asm {
        push edi
        mov edi, gameState
        xor eax, eax
        call stub
        mov zeroFlag, al
        pop edi
    }
    VirtualFree(stub, 0, MEM_RELEASE);
    return zeroFlag != 0;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_unlock_all")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(riflePurchaseCheck + 2) = 0x96;  // the unlock number 101 -> 102
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("All Unlocks: an unlock check is not the known version at 0x6267a0; the tweak is skipped") &&
              loggedText("Tweak unlock_all could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(hardDifficultyCheck) && optionIndex(L"All Unlocks") == noItem, "nothing is claimed or shown");
    codeByte(riflePurchaseCheck + 2) = 0x95;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(hardDifficultyCheck, 7) && claimed(skinColorCheck, 8) && claimed(dualSwordDrawCheck, 6) &&
              claimed(faceCheckCall, 5) && claimed(riflePurchaseCheck, 7),
        "the checks are claimed with their lengths (7, 8, 6 and 5 bytes)");
    uint32_t claimedChecks = 0;
    for (uintptr_t address : allChecks)
        if (claimedAt(address)) ++claimedChecks;
    check(claimedChecks == _countof(allChecks), "all 43 checks are claimed");
    check(!claimedAt(unlocksScreenCall) && !claimedAt(enemyDropCheck),
        "the Unlocks screen and the enemies' drops are not claimed");
    const uint32_t item = optionIndex(L"All Unlocks");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"全特典使用可",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    static uint8_t gameState[gameStateUnlocks + 0x100];
    gameState[gameStateUnlocks + hardDifficulty] = 1;  // shown, not bought
    check(codeIs(hardDifficultyCheck, {0x80, 0xbf, 0x34, 0x33, 0x04, 0x00, 0x02}) &&
              !runHardDifficultyCheck(gameState),
        "off: the game's check finds the hard difficulty not bought");
    gameState[gameStateUnlocks + hardDifficulty] = 2;
    check(runHardDifficultyCheck(gameState), "off: and bought when its state is 2");
    gameState[gameStateUnlocks + hardDifficulty] = 0;

    changeOption(item, 1);
    check(codeIs(hardDifficultyCheck, {0x39, 0xc0, 0x0f, 0x1f, 0x44, 0x00, 0x00}) &&
              runHardDifficultyCheck(gameState),
        "on: the hard difficulty is taken as bought with its state 0 (cmp eax,eax)");
    check(gameState[gameStateUnlocks + hardDifficulty] == 0, "on: the unlock's state is not written");
    check(codeIs(bodyTypeCheck, {0x39, 0xc0, 0x0f, 0x1f, 0x44, 0x00, 0x00}) &&
              codeIs(skinColorCheck, {0x39, 0xc0, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00}) &&
              codeIs(dualSwordDrawCheck, {0x39, 0xc0, 0x0f, 0x1f, 0x40, 0x00}) &&
              codeIs(faceCheckCall, {0xb0, 0x01, 0x83, 0xc4, 0x04}),
        "on: each form of check gets its patch");

    changeOption(item, 0);
    check(std::memcmp(reinterpret_cast<const void*>(va(hardDifficultyCheck)), hardDifficultyOriginal, 7) == 0 &&
              codeIs(skinColorCheck, {0x80, 0xbc, 0x0e, 0x30, 0x33, 0x04, 0x00, 0x02}) &&
              codeIs(dualSwordDrawCheck, {0x38, 0x9e, 0x96, 0x33, 0x04, 0x00}) &&
              codeIs(faceCheckCall, {0xe8, 0xe2, 0x60, 0x06, 0x00}),
        "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"unlock_all", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [unlock_all] enabled=1");

    return finish("unlock_all_native_probe");
}
