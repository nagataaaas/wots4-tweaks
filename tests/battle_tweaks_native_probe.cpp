// Installs mod.dll on a copy of the exe mapped into this process, switches the options of the Battle
// group, and checks the bytes each choice writes and writes back: the spawn interval of respawning
// enemies, their limit (also in a synthetic running manager), the clearing of defeated ones and the
// limit of things on the ground.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t spawnWaitOperand = 0x436cbb;
constexpr uintptr_t resetLimitOperand = 0x4365e3;
constexpr uintptr_t downStateOperand = 0x436ac2;
constexpr uintptr_t onScreenJump = 0x436af9;
constexpr uintptr_t firstWaitOperand = 0x43646d;
constexpr uintptr_t laterWaitOperand = 0x437165;
constexpr uintptr_t groundLimitOperand = 0x4f7796;
constexpr uintptr_t groundObjectSizeOperand = 0x4f7c1c;  // push 0x6a0
constexpr uintptr_t groundPlacesLoopCheck = 0x4f771e;    // cmp eax, 60
constexpr uintptr_t managers = 0x9e9640;

uint32_t codeUint32(uintptr_t preferred) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(va(preferred)), sizeof(value));
    return value;
}

bool onScreenJumpIsGames() { return codeByte(onScreenJump) == 0x75 && codeByte(onScreenJump + 1) == 0x4c; }

void checkSpawnInterval() {
    const uint32_t item = optionIndex(L"Enemy Spawn Interval");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 4 &&
              std::wstring(optionItem(item).label.japanese) == L"雑魚の湧く間隔",
        "Enemy Spawn Interval: a choice of 4 is passed in both languages");
    if (item == noItem) return;
    check(claimed(spawnWaitOperand, 1), "Enemy Spawn Interval: the compared count is claimed (1 byte)");
    check(codeByte(spawnWaitOperand) == 30, "Enemy Spawn Interval default: the game's 30 (31 frames a spawn)");
    changeOption(item, 1);
    check(codeByte(spawnWaitOperand) == 14, "Enemy Spawn Interval 0.5 s: 14 (15 frames a spawn)");
    changeOption(item, 2);
    check(codeByte(spawnWaitOperand) == 5, "Enemy Spawn Interval 0.2 s: 5 (6 frames a spawn)");
    changeOption(item, 3);
    check(codeByte(spawnWaitOperand) == 2, "Enemy Spawn Interval 0.1 s: 2 (3 frames a spawn)");
    changeOption(item, 0);
    check(codeByte(spawnWaitOperand) == 30, "Enemy Spawn Interval default again: the game's 30 is written back");
}

void checkSpawnLimit() {
    const uint32_t item = optionIndex(L"More Enemies at Once");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle, "More Enemies at Once: an on / off option");
    if (item == noItem) return;
    check(claimed(resetLimitOperand, 4), "More Enemies at Once: the reset limit is claimed (4 bytes)");
    check(codeUint32(resetLimitOperand) == 6, "More Enemies at Once off: the game's limit 6");

    // A running manager: [managers]+8 is the respawning enemies, whose limit is at +0x30.
    alignas(4) static uint8_t respawn[0x60] = {};
    alignas(4) static uint32_t managerList[4] = {};
    managerList[2] = reinterpret_cast<uint32_t>(respawn);
    auto& runningLimit = *reinterpret_cast<uint32_t*>(respawn + 0x30);
    *reinterpret_cast<uint32_t*>(va(managers)) = reinterpret_cast<uint32_t>(managerList);

    runningLimit = 6;
    changeOption(item, 1);
    check(codeUint32(resetLimitOperand) == 127, "More Enemies at Once on: the reset writes 127");
    check(runningLimit == 127, "on: a running mission's limit 6 becomes 127");
    changeOption(item, 0);
    check(codeUint32(resetLimitOperand) == 6 && runningLimit == 6, "off again: the code and the running limit are 6");

    runningLimit = 3;  // As SetWakiZako sets it for 3 enemies
    changeOption(item, 1);
    check(runningLimit == 3, "on: a limit a battle script set is left as it is");
    changeOption(item, 0);
    check(runningLimit == 3, "off: a limit a battle script set is left as it is");

    *reinterpret_cast<uint32_t*>(va(managers)) = 0;
    changeOption(item, 1);
    check(codeUint32(resetLimitOperand) == 127, "on without the game's managers: only the code is written");
    changeOption(item, 0);
}

void checkCorpseCleanup() {
    const uint32_t item = optionIndex(L"Defeated Enemy Cleanup");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3,
        "Defeated Enemy Cleanup: a choice of 3");
    if (item == noItem) return;
    check(claimed(downStateOperand, 4) && claimed(firstWaitOperand, 4) && claimed(laterWaitOperand, 4) &&
              claimed(onScreenJump, 2),
        "Defeated Enemy Cleanup: the state test, both waits and the on-screen jump are claimed");
    check(codeUint32(downStateOperand) == 0xc00000 && codeUint32(firstWaitOperand) == 30 &&
              codeUint32(laterWaitOperand) == 30 && onScreenJumpIsGames(),
        "Defeated Enemy Cleanup default: the game's code");

    changeOption(item, 1);
    check(codeUint32(downStateOperand) == 0xe00000, "off screen at once: dying (0x200000) counts as down");
    check(codeUint32(firstWaitOperand) == 0 && codeUint32(laterWaitOperand) == 0, "off screen at once: no wait");
    check(onScreenJumpIsGames(), "off screen at once: bodies on screen are kept");

    changeOption(item, 2);
    check(codeUint32(downStateOperand) == 0xe00000, "even on screen: dying counts as down");
    check(codeUint32(firstWaitOperand) == 30 && codeUint32(laterWaitOperand) == 30, "even on screen: the game's wait");
    check(codeByte(onScreenJump) == 0x90 && codeByte(onScreenJump + 1) == 0x90,
        "even on screen: the on-screen jump is two nops");

    changeOption(item, 0);
    check(codeUint32(downStateOperand) == 0xc00000 && codeUint32(firstWaitOperand) == 30 &&
              codeUint32(laterWaitOperand) == 30 && onScreenJumpIsGames(),
        "Defeated Enemy Cleanup default again: the game's code is written back");
}

// The game started with the default (no config.ini): the limit changes at once, the places never.
void checkGroundItemLimit() {
    const uint32_t item = optionIndex(L"Dropped Item Limit");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3,
        "Dropped Item Limit: a choice of 3");
    if (item == noItem) return;
    check(claimed(groundLimitOperand, 1), "Dropped Item Limit: the limit is claimed (1 byte)");
    check(codeByte(groundLimitOperand) == 30 && codeUint32(groundObjectSizeOperand) == 0x6a0,
        "Dropped Item Limit default: the game's 30 in 60 places");
    changeOption(item, 1);
    check(codeByte(groundLimitOperand) == 60, "Dropped Item Limit 60 at once");
    changeOption(item, 2);
    check(codeByte(groundLimitOperand) == 120, "Dropped Item Limit 120 at once");
    check(codeUint32(groundObjectSizeOperand) == 0x6a0 && codeByte(groundPlacesLoopCheck) == 0x83,
        "after a change in the game the places stay 60 until a restart");
    changeOption(item, 0);
    check(codeByte(groundLimitOperand) == 30, "Dropped Item Limit default again: the game's 30 is written back");
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_battle_tweaks")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(downStateOperand + 2) = 0x80;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Defeated Enemy Cleanup: the clearing of defeated enemies is not the known version at 0x436abc; "
                     "the tweak is skipped"),
        "the skipped tweak is logged");
    check(!claimedAt(onScreenJump) && optionIndex(L"Defeated Enemy Cleanup") == noItem, "nothing is claimed or shown");
    codeByte(downStateOperand + 2) = 0xc0;

    check(initializeMod(), "the Mod initializes on the known game code");
    checkSpawnInterval();
    checkSpawnLimit();
    checkCorpseCleanup();
    checkGroundItemLimit();

    changeOption(optionIndex(L"Enemy Spawn Interval"), 2);
    changeOption(optionIndex(L"Defeated Enemy Cleanup"), 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"enemy_spawn_interval", L"interval", 99, configPath().c_str()) == 2 &&
              GetPrivateProfileIntW(L"enemy_corpse_cleanup", L"cleanup", 99, configPath().c_str()) == 1 &&
              GetPrivateProfileIntW(L"enemy_spawn_limit", L"enabled", 99, configPath().c_str()) == 0 &&
              GetPrivateProfileIntW(L"ground_item_limit", L"limit", 99, configPath().c_str()) == 0,
        "the options save writes the four sections");

    return finish("battle_tweaks_native_probe");
}
