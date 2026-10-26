// Installs mod.dll on a copy of the exe mapped into this process, switches the option, and checks
// the bytes that mark a looted or kicked body and that take the item out of its belongings:
// written, written back, and where the draw's jump goes.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t lootMarksSearched = 0x493465;
constexpr uintptr_t kickMarksSearched = 0x51cbd8;
constexpr uintptr_t drawTakesItem = 0x4f51ec;
constexpr uintptr_t drawGaveItem = 0x4f52b1;  // pop edi; pop esi; mov al,1; ...
constexpr uint32_t markLength = 7;
constexpr uint8_t markSearched[markLength] = {0xc6, 0x81, 0x25, 0x03, 0x00, 0x00, 0x01};  // loot: [ecx+0x325]
constexpr uint8_t kickMark[markLength] = {0xc6, 0x80, 0x25, 0x03, 0x00, 0x00, 0x01};      // kick: [eax+0x325]
constexpr uint8_t sevenByteNop[markLength] = {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t takeItem[] = {0x8b, 0x96, 0xf0, 0x01, 0x00, 0x00};  // mov edx,[esi+0x1f0]
constexpr uint8_t jmpNear = 0xe9, nop = 0x90;
constexpr uintptr_t jmpNearLength = 5;

bool codeIs(uintptr_t preferred, const uint8_t* bytes, size_t length) {
    return std::memcmp(reinterpret_cast<const void*>(va(preferred)), bytes, length) == 0;
}

uintptr_t nearJumpTarget(uintptr_t preferred) {
    return preferred + jmpNearLength + *reinterpret_cast<const int32_t*>(va(preferred + 1));
}

bool gameCode() {
    return codeIs(lootMarksSearched, markSearched, markLength) && codeIs(kickMarksSearched, kickMark, markLength) &&
           codeIs(drawTakesItem, takeItem, sizeof(takeItem));
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_loot_infinite")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or written.
    codeByte(drawGaveItem + 3) = 0x00;  // mov al,0
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Endless Loot: looting is not the known version at 0x493465; the tweak is skipped") &&
              loggedText("Tweak loot_infinite could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(lootMarksSearched) && !claimedAt(drawTakesItem) && optionIndex(L"Endless Loot") == noItem,
        "nothing is claimed or shown");
    codeByte(drawGaveItem + 3) = 0x01;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(lootMarksSearched, markLength) && claimed(kickMarksSearched, markLength) &&
              claimed(drawTakesItem, sizeof(takeItem)),
        "the two marks (7 bytes each) and the draw's taking of the item (6 bytes) are claimed");
    const uint32_t item = optionIndex(L"Endless Loot");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"漁り無限",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(gameCode(), "off: the game's code is unchanged");

    changeOption(item, 1);
    check(codeIs(lootMarksSearched, sevenByteNop, markLength) && codeIs(kickMarksSearched, sevenByteNop, markLength),
        "on: looting and kicking no longer mark the body as searched");
    check(codeByte(drawTakesItem) == jmpNear && nearJumpTarget(drawTakesItem) == drawGaveItem &&
              codeByte(drawTakesItem + jmpNearLength) == nop,
        "on: the draw returns that it gave the item (0x4f52b1) without taking it from the body");

    changeOption(item, 0);
    check(gameCode(), "off again: the game's code is written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"loot_infinite", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [loot_infinite] enabled=1");

    return finish("loot_infinite_native_probe");
}
