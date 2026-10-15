// Every move card (the Move List of the status screen and the My Style cards) is filled from its
// SkillData row by 0x655db0 or 0x6567e0: both write the attack text (UI 703 "攻：%d") with
// 0x65fa60 and then call 0x65fab0(ecx = card, [esp+4] = &row+0xe) to choose the icons. The hook
// adds the mark to the attack text after the icons are chosen, from the same row.
//
// The damage calculations (0x43e9f8, 0x49d4cb) add the Big Shot charm's value to the move's power
// when the row's attribute bits have 0x0008 or 0x0200 (Defense Break). The power is then
// multiplied by the weapon's attack power (0x49cec0) unless the row's byte +0x1b is set.
#include "big_shot_marks.h"
#include <cwchar>
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t chooseCardIcons = 0x65fab0;
constexpr uint32_t chooseCardIconsClaim = 6;  // push ebp; mov ebp,esp; and esp,-8
constexpr uintptr_t writeCardAttack = 0x65fa60;
// Where the two card fillers write the attack and then choose the icons.
constexpr uintptr_t cardFillers[] = {0x655e0e, 0x656878};
constexpr uintptr_t bigShotChecks[] = {0x43e9f8, 0x49d4cb};
constexpr uintptr_t weaponMultiplierCheck = 0x43e9b8;

// The card's attack text: wchar_t[0x40] at +0x19c (cleared with 0x80 bytes at 0x65eddf).
constexpr uint32_t cardAttackText = 0x19c;
constexpr size_t attackTextLength = 0x40;
// Offsets from the icons' argument, which points at SkillData +0xe.
constexpr uint32_t rowAttributes = 0;                // u16
constexpr uint32_t rowIgnoresWeapon = 0x1b - 0xe;     // u8, nonzero: the weapon does not multiply
constexpr uint16_t bigShotAttributes = 0x0008 | 0x0200;

constexpr wchar_t firstJapaneseCharacter = 0x3000;

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Big Shot in Move List");

using ChooseCardIcons = void(__fastcall*)(uint8_t* card, void* unusedEdx, const uint8_t* row);
ChooseCardIcons originalChooseCardIcons;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff};
    if (!game.codeIs(chooseCardIcons, prologue, sizeof(prologue))) return false;
    // movsx edx,word [esi+0x1e]; mov ecx,edi; call 0x65fa60; add esi,0xe; push esi; mov ecx,edi; call 0x65fab0
    const uint8_t writesAttack[] = {0x0f, 0xbf, 0x56, 0x1e, 0x8b, 0xcf};
    const uint8_t passesRow[] = {0x83, 0xc6, 0x0e, 0x56, 0x8b, 0xcf};
    for (uintptr_t filler : cardFillers) {
        if (!game.codeIs(filler, writesAttack, sizeof(writesAttack)) || !game.callsTo(filler + 6, writeCardAttack) ||
            !game.codeIs(filler + 11, passesRow, sizeof(passesRow)) || !game.callsTo(filler + 17, chooseCardIcons))
            return false;
    }
    // movzx eax,word [reg+0xe]; test al,8; jne; test eax,0x200
    const uint8_t testsBit8[] = {0xa8, 0x08, 0x75, 0x07, 0xa9, 0x00, 0x02, 0x00, 0x00};
    for (uintptr_t check : bigShotChecks) {
        if (!game.codeIs(check + 4, testsBit8, sizeof(testsBit8))) return false;
    }
    // movsx edx,word [esi+0x1c]; movzx eax,byte [esi+0x1b]
    const uint8_t readsMultiplierFlag[] = {0x0f, 0xbf, 0x56, 0x1c, 0x0f, 0xb6, 0x46, 0x1b};
    return game.codeIs(weaponMultiplierCheck, readsMultiplierFlag, sizeof(readsMultiplierFlag));
}

// 重 / BS: the weapon also multiplies the charm's value; 重△ / BS-: it is added as it is, so the
// charm helps these moves less. △ is in no game text, so English uses an ASCII mark.
const wchar_t* markFor(const uint8_t* row, bool japanese) {
    const uint16_t attributes = *reinterpret_cast<const uint16_t*>(row + rowAttributes);
    if ((attributes & bigShotAttributes) == 0) return nullptr;
    const bool weaponMultiplies = row[rowIgnoresWeapon] == 0;
    if (japanese) return weaponMultiplies ? L" 重" : L" 重△";
    return weaponMultiplies ? L" BS" : L" BS-";
}

// Runs once per card when a chart is built, not each frame. The game shrinks a long attack text
// to the card's width when it draws it, so a mark that fits the buffer stays on the card.
void addMark(uint8_t* card, const uint8_t* row) {
    wchar_t* attack = reinterpret_cast<wchar_t*>(card + cardAttackText);
    if (attack[0] == 0) return;  // Moves without an attack (defense moves) show none
    const wchar_t* mark = markFor(row, attack[0] >= firstJapaneseCharacter);
    if (!mark) return;
    if (wcsnlen(attack, attackTextLength) + wcslen(mark) >= attackTextLength) return;
    wcscat_s(attack, attackTextLength, mark);
}

void __fastcall chooseCardIconsHook(uint8_t* card, void* unusedEdx, const uint8_t* row) {
    originalChooseCardIcons(card, unusedEdx, row);
    if (enabled == toggleOn && row) addMark(card, row);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the move card code is not the known version", chooseCardIcons);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, chooseCardIcons, chooseCardIconsClaim,
            reinterpret_cast<void*>(&chooseCardIconsHook), reinterpret_cast<void**>(&originalChooseCardIcons))) {
        logger.installFailed(error, chooseCardIcons);
        return false;
    }
    return true;
}

// 重撃 is "Big Shot" and 技表 is "Move List" in the game's English texts.
const Option options[] = {
    toggle(L"enabled", {L"技表に重撃表示", L"Big Shot in Move List"}, &enabled, false),
};

}  // namespace

const Tweak bigShotMarksTweak{L"big_shot_marks", options, 1, install};

}  // namespace tweaks
