// Installs mod.dll on a copy of the exe mapped into this process and calls the hooked icon chooser
// 0x65fab0 with a synthetic move card and SkillData row. The rest of the game's function (after the
// 6 bytes the hook copies) is made to return at once, so only the tweak's mark is seen.
#include <cstdio>
#include <cstring>
#include <string>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t chooseCardIcons = 0x65fab0;
constexpr uintptr_t afterCopiedPrologue = 0x65fab6;
constexpr uint32_t cardSize = 0x25c;
constexpr uint32_t cardAttackText = 0x19c;
constexpr size_t attackTextLength = 0x40;
constexpr uint32_t rowSize = 0x70;
constexpr uint32_t rowAttributes = 0xe;
constexpr uint32_t rowIgnoresWeapon = 0x1b;
constexpr uint16_t defenseBreak = 0x0200;
constexpr uint16_t bit8 = 0x0008;
constexpr uint16_t throwAttribute = 0x0010;

alignas(4) uint8_t card[cardSize];
alignas(4) uint8_t row[rowSize];

wchar_t* attackText() { return reinterpret_cast<wchar_t*>(card + cardAttackText); }

// mov esp,ebp; pop ebp; ret 4: the end of 0x65fab0 without its work.
void returnAfterPrologue() {
    const uint8_t epilogue[] = {0x8b, 0xe5, 0x5d, 0xc2, 0x04, 0x00};
    std::memcpy(&codeByte(afterCopiedPrologue), epilogue, sizeof(epilogue));
}

using ChooseCardIcons = void(__fastcall*)(uint8_t* card, void* unusedEdx, const uint8_t* row);

std::wstring markedText(const wchar_t* attack, uint16_t attributes, bool ignoresWeapon) {
    std::memset(card, 0, sizeof(card));
    std::memset(row, 0, sizeof(row));
    wcscpy_s(attackText(), attackTextLength, attack);
    *reinterpret_cast<uint16_t*>(row + rowAttributes) = attributes;
    row[rowIgnoresWeapon] = ignoresWeapon ? 1 : 0;
    reinterpret_cast<ChooseCardIcons>(va(chooseCardIcons))(card, nullptr, row + rowAttributes);
    return attackText();
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_big_shot_marks")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the damage calculation tests another bit, so the marks would be wrong.
    codeByte(0x43e9fd) = 0x10;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Big Shot in Move List: the move card code is not the known version at 0x65fab0; the tweak "
                     "is skipped") &&
              loggedText("Tweak big_shot_marks could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(chooseCardIcons) && codeByte(chooseCardIcons) == 0x55 &&
              optionIndex(L"Big Shot in Move List") == noItem,
        "nothing is claimed, hooked or shown");
    codeByte(0x43e9fd) = 0x08;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(chooseCardIcons, 6) && codeByte(chooseCardIcons) == jumpOpcode,
        "the icon chooser is hooked (6 bytes claimed)");
    const uint32_t item = optionIndex(L"Big Shot in Move List");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"技表に重撃表示",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    returnAfterPrologue();
    check(markedText(L"攻：120", defenseBreak, false) == L"攻：120", "off: the attack text is left as it is");

    changeOption(item, 1);
    check(markedText(L"攻：120", defenseBreak, false) == L"攻：120 重",
        "on: a Defense Break move multiplied by the weapon gets 重");
    check(markedText(L"攻：80", bit8, true) == L"攻：80 重△", "on: a bit 0x0008 move not multiplied gets 重△");
    check(markedText(L"攻：90", bit8 | defenseBreak | throwAttribute, false) == L"攻：90 重",
        "on: other bits beside them do not matter");
    check(markedText(L"攻：120", throwAttribute, true) == L"攻：120",
        "on: a move without either bit gets no mark, even when not multiplied");
    check(markedText(L"Str: 120", defenseBreak, false) == L"Str: 120 BS" &&
              markedText(L"Str: 80", bit8, true) == L"Str: 80 BS-",
        "on: English attack texts get BS and BS-");
    check(markedText(L"", defenseBreak, false).empty(), "on: a card without an attack text gets no mark");
    const std::wstring nearlyFull(attackTextLength - 3, L'9');
    check(markedText(nearlyFull.c_str(), defenseBreak, true) == nearlyFull,
        "on: a mark that would not fit the 0x40 characters is left out");

    changeOption(item, 0);
    check(markedText(L"攻：120", defenseBreak, false) == L"攻：120", "off again: no mark");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"big_shot_marks", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [big_shot_marks] enabled=1");

    return finish("big_shot_marks_native_probe");
}
