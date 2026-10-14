// An unlock's state is one byte at game state +0x43330 + its number (0 to 104): 2 when bought.
// The game has no single check to hook: an inline function of GameParamManager.cpp compares the
// byte with 2 where it is used. Each of those comparisons is patched to cmp eax,eax, which sets
// the flags as "equal to 2" does without reading the byte.
//
// Left as the game has them: the Unlocks screen (its conditions, display and buying), counting
// unlocks and styles for achievements, adding the dual-sword style on loading or buying, and
// which weapons enemies drop. The weapon chests and the accessories of unlocks are not checked
// when used (buying them enlarges the chest or sets the accessory as owned), so they are not here.
#include "unlock_all.h"
#include <vector>
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uint32_t unlockStates = 0x43330;  // In the game state ([0x9e967c])
constexpr uintptr_t isUnlocked = 0x4dba10;  // esi = number, push game state; ret 4; al = bought

enum class CheckForm {
    compareWithTwo,         // cmp byte [reg+0x43330+n],2 (80 modRm disp32 02)
    compareIndexedWithTwo,  // cmp byte [esi+ecx+0x43330],2 (80 bc 0e disp32 02)
    compareWithBl,          // cmp byte [reg+0x43330+n],bl, with bl = 2 (38 modRm disp32)
    callIsUnlocked,         // call 0x4dba10, after a push of the game state
};

struct UnlockCheck {
    uintptr_t address;
    CheckForm form;
    uint8_t modRm;    // For compareWithTwo and compareWithBl
    uint8_t unlock;  // For compareWithTwo and compareWithBl
};

// Unlock numbers (GameTextUnlock order, from 0)
enum Unlock : uint8_t {
    hardDifficulty = 4,
    veryHardDifficulty = 5,
    bodyTypeChange = 16,
    gunLicense = 100,
    rifleLicense = 101,
    dualSwordLicense = 102,
    dualSwordLicenseExtra = 103,
    chiefMinisterGun = 104,
};

constexpr uint8_t viaEsi = 0xbe;
constexpr uint8_t viaEdi = 0xbf;
constexpr uint8_t viaEbx = 0xbb;
constexpr uint8_t blViaEsi = 0x9e;

const UnlockCheck checks[] = {
    // Choosing the difficulty (GamePartTitle.cpp)
    {0x4ed74e, CheckForm::compareWithTwo, viaEdi, hardDifficulty},
    {0x4ed7cc, CheckForm::compareWithTwo, viaEdi, veryHardDifficulty},
    // The body type change in the character setup (BodyEdit.cpp)
    {0x5f4eb5, CheckForm::compareWithTwo, viaEbx, bodyTypeChange},
    {0x5f4faf, CheckForm::compareWithTwo, viaEbx, bodyTypeChange},
    // Lists of looks filtered by the unlock in their record (skin and hair colors)
    {0x4755f4, CheckForm::compareIndexedWithTwo, 0, 0},
    {0x4756d4, CheckForm::compareIndexedWithTwo, 0, 0},
    // Whether a face, hairstyle or clothes can be chosen (CharacterModelManager.cpp)
    {0x475929, CheckForm::callIsUnlocked, 0, 0},
    {0x479df6, CheckForm::callIsUnlocked, 0, 0},
    // "You cannot use two swords yet" (UIManager.cpp, CharacterCtrlInput.cpp)
    {0x4626e1, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x462713, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
    {0x463850, CheckForm::compareWithTwo, viaEdi, dualSwordLicense},
    {0x463882, CheckForm::compareWithTwo, viaEdi, dualSwordLicenseExtra},
    // Drawing two swords (CharacterParameter.cpp)
    {0x499c65, CheckForm::compareWithBl, blViaEsi, dualSwordLicense},
    {0x499c96, CheckForm::compareWithBl, blViaEsi, dualSwordLicenseExtra},
    // Equipping guns, rifles and two swords (CharacterModelSet.cpp)
    {0x496fa9, CheckForm::compareWithTwo, viaEsi, rifleLicense},
    {0x496ffb, CheckForm::compareWithTwo, viaEsi, gunLicense},
    {0x497213, CheckForm::compareWithTwo, viaEdi, rifleLicense},
    {0x49724e, CheckForm::compareWithTwo, viaEdi, gunLicense},
    {0x4975ca, CheckForm::compareWithTwo, viaEdi, rifleLicense},
    {0x497607, CheckForm::compareWithTwo, viaEdi, gunLicense},
    {0x499838, CheckForm::compareWithTwo, viaEsi, rifleLicense},
    {0x499871, CheckForm::compareWithTwo, viaEsi, gunLicense},
    {0x4979f5, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x497a27, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
    // Whether a style of guns, rifles or two swords can be used (SectUtility, 0x5939f0)
    {0x593a5f, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x593a8e, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
    {0x593aba, CheckForm::compareWithTwo, viaEsi, gunLicense},
    {0x593ae6, CheckForm::compareWithTwo, viaEsi, rifleLicense},
    // The weapon kinds when making a My Style (CreateSectStyleSelect.cpp, two functions)
    {0x5d0a16, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x5d0a54, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
    {0x5d0aa4, CheckForm::compareWithTwo, viaEsi, gunLicense},
    {0x5d0ae2, CheckForm::compareWithTwo, viaEsi, rifleLicense},
    {0x5d1697, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x5d16d4, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
    {0x5d1722, CheckForm::compareWithTwo, viaEsi, gunLicense},
    {0x5d175f, CheckForm::compareWithTwo, viaEsi, rifleLicense},
    // Buying guns and rifles (ShopBuy.cpp, ShopDrop.cpp)
    {0x620428, CheckForm::compareWithTwo, viaEdi, gunLicense},
    {0x62045a, CheckForm::compareWithTwo, viaEdi, rifleLicense},
    {0x62676e, CheckForm::compareWithTwo, viaEdi, gunLicense},
    {0x6267a0, CheckForm::compareWithTwo, viaEdi, rifleLicense},
    // The lost-and-found shop selling the chief minister's gun (ShopDrop.cpp)
    {0x6275ce, CheckForm::compareWithTwo, viaEsi, chiefMinisterGun},
    // The interface's "two swords can be used" (UIManager.cpp)
    {0x6710b1, CheckForm::compareWithTwo, viaEsi, dualSwordLicense},
    {0x6710dd, CheckForm::compareWithTwo, viaEsi, dualSwordLicenseExtra},
};
constexpr size_t checkCount = _countof(checks);

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("All Unlocks");
std::vector<CodePatch> patches;  // One per check, made at install
bool installed = false;

bool compareMatches(const UnlockCheck& check, uint8_t opcode, bool endsWithTwo) {
    const uint8_t prefix[] = {opcode, check.modRm};
    constexpr uint8_t two = 2;
    return game.codeIs(check.address, prefix, sizeof(prefix)) &&
           game.readUint32(check.address + 2) == unlockStates + check.unlock &&
           (!endsWithTwo || game.codeIs(check.address + 6, &two, 1));
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool checkMatches(const UnlockCheck& check) {
    switch (check.form) {
    case CheckForm::compareWithTwo:
        return compareMatches(check, 0x80, true);
    case CheckForm::compareIndexedWithTwo: {
        const uint8_t compare[] = {0x80, 0xbc, 0x0e, 0x30, 0x33, 0x04, 0x00, 0x02};
        return game.codeIs(check.address, compare, sizeof(compare));
    }
    case CheckForm::compareWithBl:
        return compareMatches(check, 0x38, false);
    case CheckForm::callIsUnlocked: {
        const uint8_t testAl[] = {0x84, 0xc0};  // test al,al right after
        return game.callsTo(check.address, isUnlocked) && game.codeIs(check.address + 5, testAl, sizeof(testAl));
    }
    }
    return false;
}

// cmp eax,eax and a nop of the rest: ZF set and CF clear, as comparing 2 with 2 leaves them.
CodePatch patchFor(const UnlockCheck& check) {
    switch (check.form) {
    case CheckForm::compareWithTwo:
        return CodePatch(check.address, {0x39, 0xc0, 0x0f, 0x1f, 0x44, 0x00, 0x00});
    case CheckForm::compareIndexedWithTwo:
        return CodePatch(check.address, {0x39, 0xc0, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00});
    case CheckForm::compareWithBl:
        return CodePatch(check.address, {0x39, 0xc0, 0x0f, 0x1f, 0x40, 0x00});
    case CheckForm::callIsUnlocked:
        // mov al,1; add esp,4: the call's result, and the push the call's ret 4 would remove.
        // The test al,al after it sets the flags.
        return CodePatch(check.address, {0xb0, 0x01, 0x83, 0xc4, 0x04});
    }
    return CodePatch(check.address, {});
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    for (const UnlockCheck& check : checks) {
        if (!checkMatches(check)) {
            logger.installFailed("an unlock check is not the known version", check.address);
            return false;
        }
    }
    patches.clear();
    patches.reserve(checkCount);
    for (const UnlockCheck& check : checks) patches.push_back(patchFor(check));
    if (!claimPatches(context, game, patches.data(), patches.size(), logger)) return false;
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (installed) writePatches(patches.data(), patches.size(), value == toggleOn, logger);
}

// 特典 is "Unlocks" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"全特典使用可", L"All Unlocks"}, &enabled, false, applied),
};

}  // namespace

const Tweak unlockAllTweak{L"unlock_all", options, 1, install};

}  // namespace tweaks
