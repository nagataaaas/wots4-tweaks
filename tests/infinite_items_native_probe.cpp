// Installs mod.dll on a copy of the exe mapped into this process and runs the two places where
// the player's items lose a unit, with synthetic characters, inventories and game tables:
// - The decrement and the erase of the slot in 0x4fc150, run from 0x4fc405 with the function's
//   frame laid out on the stack. A ret written at 0x4fc435 (before the item screen is refreshed)
//   returns to the test; the erase's memmove import is pointed at the CRT's.
// - 0x4fadb0 called by command 96 (from 0x40dd6f, with a ret written at 0x40dd7b after the call)
//   and by anything else (called directly). The character is not the player so that the game's
//   update of the item screen (0x4f8420) is not run; command 96 is never told apart by character.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t decrementCount = 0x4fc40e;
constexpr uintptr_t decrementStart = 0x4fc405;
constexpr uintptr_t afterSlotUpdate = 0x4fc435;
constexpr uintptr_t memmoveImport = 0x9052cc;
constexpr uintptr_t removeItemListEntry = 0x4fadb0;
constexpr uintptr_t scriptRemove = 0x40dd6f;
constexpr uintptr_t afterScriptRemove = 0x40dd7b;
constexpr uintptr_t gameParametersGlobal = 0x9e967c;
constexpr uintptr_t itemListGlobal = 0x9e96a0;
constexpr uintptr_t itemDataGlobal = 0x9e96ac;

constexpr uint32_t parametersOffset = 0xc;
constexpr uint32_t idOffset = 4;
constexpr uint32_t inventoryBeginOffset = 0x1ec;
constexpr uint32_t inventoryEndOffset = 0x1f0;
constexpr int32_t playerId = 0;
constexpr int32_t otherId = 7;

constexpr int16_t onigiriRow = 1;
constexpr int16_t onigiriId = 2;
constexpr int16_t keyRow = 674;
constexpr int16_t keyId = 0xed;
constexpr int16_t eatMotion = 0xd5;
constexpr int16_t noUseMotion = -1;
constexpr int16_t itemKind = 6;

struct InventorySlot {
    int16_t id;
    uint16_t count;
    int8_t kind;
    uint8_t unused[3];
};

struct Character {
    alignas(16) uint8_t object[0x40] = {};
    alignas(16) uint8_t parameters[0x400] = {};
    InventorySlot slots[2] = {};
    explicit Character(int32_t id) {
        auto* parametersAddress = parameters;
        std::memcpy(object + parametersOffset, &parametersAddress, sizeof(parametersAddress));
        std::memcpy(parameters + idOffset, &id, sizeof(id));
        InventorySlot* begin = slots;
        InventorySlot* end = slots + 2;
        std::memcpy(parameters + inventoryBeginOffset, &begin, sizeof(begin));
        std::memcpy(parameters + inventoryEndOffset, &end, sizeof(end));
        slots[0] = {onigiriId, 3, itemKind, {}};
        slots[1] = {keyId, 2, itemKind, {}};  // 2, so taking one leaves the slot (0x4a1d30 is not run)
    }
    size_t slotCount() const {
        InventorySlot* begin;
        InventorySlot* end;
        std::memcpy(&begin, parameters + inventoryBeginOffset, sizeof(begin));
        std::memcpy(&end, parameters + inventoryEndOffset, sizeof(end));
        return static_cast<size_t>(end - begin);
    }
};

// One-node std::map as the game lays it out: the header's +4 is the root; a node has the left
// child at +8, the right at +0xc, the int16 key at +0x10 and the value at +0x14.
struct MapNode {
    uint32_t unused0;
    MapNode* parent;
    MapNode* left;
    MapNode* right;
    int16_t key;
    int16_t padding;
    const void* value;
};

struct TwoEntryMap {
    MapNode header{};
    MapNode low{};
    MapNode high{};
    TwoEntryMap(int16_t lowKey, const void* lowValue, int16_t highKey, const void* highValue) {
        header.parent = &low;
        low = {0, &header, nullptr, &high, lowKey, 0, lowValue};
        high = {0, &low, nullptr, nullptr, highKey, 0, highValue};
    }
};

int16_t onigiriListEntry[2] = {itemKind, onigiriId};
int16_t keyListEntry[2] = {itemKind, keyId};
alignas(4) uint8_t onigiriData[0x40];
alignas(4) uint8_t keyData[0x40];
alignas(4) uint8_t gameParameters[0x10000];

void setUseMotion(uint8_t* itemData, int16_t motion) { std::memcpy(itemData + 0x24, &motion, sizeof(motion)); }

// Runs 0x4fc405..0x4fc42a on slot 0: ebx points at the inventory's begin, [esp+0x14] is the slot's
// offset and [esp+0x1c] the character, as in 0x4fc150's frame.
void runDecrement(Character& character) {
    const uintptr_t start = va(decrementStart);
    uint8_t* characterObject = character.object;
    uint8_t* inventoryBegin = character.parameters + inventoryBeginOffset;
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, inventoryBegin
        push characterObject  // [esp+0x1c] at the code
        push 0                // [esp+0x18]
        push 0                // [esp+0x14]: slot 0
        push 0
        push 0
        push 0
        push 0
        call start
        add esp, 0x1c
        pop edi
        pop esi
        pop ebx
    }
}

void removeAsScript(Character& character, int16_t row) {
    const uintptr_t start = va(scriptRemove);
    uint8_t* characterObject = character.object;
    const int32_t rowArgument = row;
    __asm {
        push esi
        push edi
        mov eax, characterObject
        mov edi, rowArgument
        call start
        pop edi
        pop esi
    }
}

void removeDirectly(Character& character, int16_t row) {
    using Remove = void(__cdecl*)(int32_t row, int32_t count, uint8_t* character);
    reinterpret_cast<Remove>(va(removeItemListEntry))(row, 1, character.object);
}

void setGameTables(const TwoEntryMap& itemList, const TwoEntryMap& itemData) {
    *reinterpret_cast<const void**>(va(itemListGlobal)) = &itemList.header;
    *reinterpret_cast<const void**>(va(itemDataGlobal)) = &itemData.header;
    *reinterpret_cast<void**>(va(gameParametersGlobal)) = gameParameters;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_infinite_items")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or hooked.
    codeByte(decrementCount + 4) = 0xc0;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Infinite Items: the use of items is not the known version at 0x4fc40e; the tweak is skipped"),
        "the skipped tweak is logged");
    check(!claimedAt(decrementCount) && !claimedAt(removeItemListEntry) && optionIndex(L"Infinite Items") == noItem,
        "nothing is claimed or shown");
    codeByte(decrementCount + 4) = 0xc8;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(decrementCount, 5) && codeByte(decrementCount) == jumpOpcode, "the decrement is hooked");
    check(claimed(removeItemListEntry, 6) && codeByte(removeItemListEntry) == jumpOpcode,
        "the removal of an ItemList entry is hooked");
    const uint32_t item = optionIndex(L"Infinite Items");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"アイテム減らない",
        "a toggle アイテム減らない / Infinite Items is passed");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    constexpr uint32_t off = 0;
    constexpr uint32_t on = 1;

    codeByte(afterSlotUpdate) = returnOpcode;
    *reinterpret_cast<void**>(va(memmoveImport)) = reinterpret_cast<void*>(&std::memmove);
    codeByte(afterScriptRemove) = returnOpcode;
    setUseMotion(onigiriData, eatMotion);
    setUseMotion(keyData, noUseMotion);
    const TwoEntryMap itemList(onigiriRow, onigiriListEntry, keyRow, keyListEntry);
    const TwoEntryMap itemData(onigiriId, onigiriData, keyId, keyData);
    setGameTables(itemList, itemData);

    {
        // The game erases the slot after every use that took effect, whatever count is left.
        Character player(playerId);
        runDecrement(player);
        check(player.slotCount() == 1 && player.slots[0].id == keyId, "off: using an item erases its slot");
    }
    {
        Character other(otherId);
        removeAsScript(other, onigiriRow);
        check(other.slots[0].count == 2, "off: a script takes an onigiri");
    }

    changeOption(item, on);
    {
        Character player(playerId);
        runDecrement(player);
        check(player.slotCount() == 2 && player.slots[0].id == onigiriId && player.slots[0].count == 3,
            "on: the player keeps a used item");
        player.slots[0].count = 1;
        runDecrement(player);
        check(player.slotCount() == 2 && player.slots[0].id == onigiriId && player.slots[0].count == 1,
            "on: the player keeps the last unit");
        Character other(otherId);
        runDecrement(other);
        check(other.slotCount() == 1 && other.slots[0].id == keyId, "on: another character still uses up its item");
    }
    {
        Character other(otherId);
        removeAsScript(other, onigiriRow);
        check(other.slots[0].count == 3, "on: a script does not take a usable item (onigiri)");
        removeAsScript(other, keyRow);
        check(other.slots[1].count == 1,
            "on: a script still takes an item not used from the bag (a key)");
        removeDirectly(other, onigiriRow);
        check(other.slots[0].count == 2, "on: a removal not by command 96 (a sale, for example) still takes the item");
    }

    changeOption(item, off);
    {
        Character player(playerId);
        runDecrement(player);
        check(player.slotCount() == 1, "off again: using an item erases its slot");
    }

    changeOption(item, on);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"infinite_items", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [infinite_items] enabled=1");
    return finish("infinite_items_native_probe");
}
