// Installs mod.dll on a copy of the exe mapped into this process and calls the game's loss of
// durability (0x693bf0, now hooked) with a synthetic weapon and characters: with No Loss the
// player's weapon keeps its durability while another character's loses it as before; with Break at
// Once the game is given 32767. Breaking runs code these synthetic objects cannot (the weapon's
// vtable, the message at each hundred), so that choice is checked through the game's own factor of
// [parameters+0x250] (when [parameters+0x27c] has 0x10), which makes 3 lose 0 and 32767 lose 32.
// The test fills the import of floor ([0x9051b0]) that the factor uses.
#include <cstdio>
#include <cmath>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t loseDurability = 0x693bf0;
constexpr uint32_t durabilityOffset = 0x58;      // int16 in a 32-bit field
constexpr uint32_t maxDurabilityOffset = 0x3e;   // int16
constexpr uint32_t parametersOffset = 0xc;
constexpr uint32_t idOffset = 4;
constexpr int32_t playerId = 0;
constexpr int32_t otherId = 7;
constexpr int32_t startDurability = 550;
constexpr int32_t amount = 3;  // Stays within the same hundred, so no message and no break

constexpr uint32_t lossFactorFlagsOffset = 0x27c;
constexpr uint8_t lossFactorFlag = 0x10;
constexpr uint32_t lossFactorOffset = 0x250;  // float; the loss becomes floor(loss * factor)
constexpr float tinyLossFactor = 0.001f;
constexpr int32_t scaledBreakingLoss = 32;  // floor(32767 * 0.001)
constexpr uintptr_t floorImport = 0x9051b0;  // MSVCR100 floor

struct Character {
    alignas(16) uint8_t object[0x40] = {};
    alignas(16) uint8_t parameters[0x400] = {};
    Character(int32_t id) {
        auto* parametersAddress = parameters;
        std::memcpy(object + parametersOffset, &parametersAddress, sizeof(parametersAddress));
        std::memcpy(parameters + idOffset, &id, sizeof(id));
    }
    void scaleLossBy(float factor) {
        parameters[lossFactorFlagsOffset] |= lossFactorFlag;
        std::memcpy(parameters + lossFactorOffset, &factor, sizeof(factor));
    }
};

struct Weapon {
    alignas(16) uint8_t bytes[0x100] = {};
    Weapon() {
        const int16_t maxDurability = 1000;
        std::memcpy(bytes + maxDurabilityOffset, &maxDurability, sizeof(maxDurability));
        setDurability(startDurability);
    }
    void setDurability(int32_t value) { std::memcpy(bytes + durabilityOffset, &value, sizeof(value)); }
    int32_t durability() const {
        int32_t value;
        std::memcpy(&value, bytes + durabilityOffset, sizeof(value));
        return value;
    }
};

// The weapon in hand: [parameters+0x2c4] is its slot, and [parameters+0x18c + slot*8] the weapon.
constexpr uint32_t handSlotOffset = 0x2c4;
constexpr uint32_t otherHandSlotOffset = 0x2c8;
constexpr uint32_t weaponSlotsOffset = 0x18c;
constexpr int32_t noSlot = -1;

void holdWeapon(Character& character, Weapon& weapon) {
    const int32_t firstSlot = 0;
    uint8_t* weaponBytes = weapon.bytes;
    std::memcpy(character.parameters + handSlotOffset, &firstSlot, sizeof(firstSlot));
    std::memcpy(character.parameters + otherHandSlotOffset, &noSlot, sizeof(noSlot));
    std::memcpy(character.parameters + weaponSlotsOffset, &weaponBytes, sizeof(weaponBytes));
}

// The characters' vtable +0x38 (0x4991a0, thiscall: amount, which hand (0 the first), unused),
// which guns take instead of +0x34 (0x499070 sends weapon types 8 and 9 there).
constexpr uintptr_t loseGunDurability = 0x4991a0;

uint8_t callLoseGunDurability(Character& character, int32_t lost) {
    const uintptr_t function = va(loseGunDurability);
    uint8_t* characterObject = character.object;
    uint8_t broke;
    __asm {
        push 0
        push 0
        push lost
        mov ecx, characterObject
        call function
        mov broke, al
    }
    return broke;
}

// Returns what the game returns: 1 if the weapon broke.
uint8_t callLoseDurability(Weapon& weapon, Character& character, int32_t lost) {
    const uintptr_t function = va(loseDurability);
    uint8_t* weaponBytes = weapon.bytes;
    uint8_t* characterObject = character.object;
    uint8_t broke;
    __asm {
        push esi
        mov esi, weaponBytes
        push characterObject
        push lost
        call function
        mov broke, al
        pop esi
    }
    return broke;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_weapon_durability")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or hooked.
    codeByte(0x693c26 + 3) = 0x5c;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Weapon Durability: the loss of weapon durability is not the known version at 0x693bf0; "
                     "the tweak is skipped"),
        "the skipped tweak is logged");
    check(!claimedAt(loseDurability) && optionIndex(L"Weapon Durability") == noItem, "nothing is claimed or shown");
    codeByte(0x693c26 + 3) = 0x58;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(loseDurability, 6) && codeByte(loseDurability) == jumpOpcode, "the function's entry is hooked");
    const uint32_t item = optionIndex(L"Weapon Durability");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"武器耐久値" &&
              std::wstring(optionItem(item).choices[2].japanese) == L"即破損",
        "a choice of 3 (デフォルト / 減らない / 即破損) is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is the default");
    constexpr uint32_t gameDefault = 0;
    constexpr uint32_t noLoss = 1;
    constexpr uint32_t breakAtOnce = 2;

    Character player(playerId);
    Character other(otherId);
    Weapon weapon;

    check(callLoseDurability(weapon, player, amount) == 0 && weapon.durability() == startDurability - amount,
        "default: the player's weapon loses durability as in the game");

    changeOption(item, noLoss);
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, player, amount) == 0 && weapon.durability() == startDurability,
        "no loss: the player's weapon keeps its durability");
    weapon.setDurability(2);
    check(callLoseDurability(weapon, player, 50) == 0 && weapon.durability() == 2,
        "no loss: a nearly broken weapon of the player does not break");
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, other, amount) == 0 && weapon.durability() == startDurability - amount,
        "no loss: another character's weapon still loses durability");

    Weapon gun;
    holdWeapon(player, gun);
    check(callLoseGunDurability(player, amount) == 0 && gun.durability() == startDurability,
        "no loss: the player's gun (the +0x38 path) keeps its durability");
    holdWeapon(other, gun);
    check(callLoseGunDurability(other, amount) == 0 && gun.durability() == startDurability - amount,
        "no loss: another character's gun still loses durability");

    // floor(3 * 0.001) = 0, floor(32767 * 0.001) = 32
    double (*const floorFunction)(double) = &std::floor;
    *reinterpret_cast<uintptr_t*>(va(floorImport)) = reinterpret_cast<uintptr_t>(floorFunction);
    Character scaledPlayer(playerId);
    Character scaledOther(otherId);
    scaledPlayer.scaleLossBy(tinyLossFactor);
    scaledOther.scaleLossBy(tinyLossFactor);
    changeOption(item, gameDefault);
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, scaledOther, amount) == 0 && weapon.durability() == startDurability,
        "default with the game's factor: 3 loses 0");
    changeOption(item, breakAtOnce);
    weapon.setDurability(startDurability);
    const int32_t afterBreakingLoss = startDurability - scaledBreakingLoss;
    check(callLoseDurability(weapon, scaledOther, amount) == 0 && weapon.durability() == afterBreakingLoss,
        "break at once: another character's loss is made 32767 (loses 32 with the factor)");
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, scaledPlayer, amount) == 0 && weapon.durability() == afterBreakingLoss,
        "break at once: the player's loss is made 32767 too");
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, scaledOther, 0) == 0 && weapon.durability() == startDurability,
        "break at once: a loss of 0 stays 0");

    changeOption(item, gameDefault);
    weapon.setDurability(startDurability);
    check(callLoseDurability(weapon, player, amount) == 0 && weapon.durability() == startDurability - amount,
        "default again: the player's weapon loses durability");

    changeOption(item, breakAtOnce);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"weapon_durability", L"durability", 99, configPath().c_str()) == 2,
        "the options save writes [weapon_durability] durability=2");
    return finish("weapon_durability_native_probe");
}
