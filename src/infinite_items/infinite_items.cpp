// Items lose a unit in two places this tweak hooks:
// - 0x4fc150 (ECX = item ID, EDX = character) applies an item's effects and, only when they took
//   effect, lowers the count of its slot by one at 0x4fc40e and then erases the slot at 0x4fc430.
//   Using an item from the bag or the quick slots and Medicine used when health runs out
//   (0x4fc610) all go through it.
// - Action script command 96 (0x40dcc0) takes items from the player by calling 0x4fadb0 (cdecl:
//   ItemList row, count, character) once per unit; missions such as Gramps' onigiri use it.
// Only the player keeps items: other characters use theirs up as before. A script takes only
// items that can be used from the bag (food and the like) without lowering the count; keys and
// other things handed over are taken as before, so missions still move on.
#include "infinite_items.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t decrementCount = 0x4fc40e;  // mov al,[ecx+2]; dec al
constexpr uint32_t decrementCountLength = 5;
constexpr uintptr_t eraseSlotCall = 0x4fc430;  // call 0x4a1d30 (erase from the inventory vector)
constexpr uintptr_t eraseSlot = 0x4a1d30;
constexpr uintptr_t afterSlotUpdate = 0x4fc435;  // Refreshes the item screen for the player
constexpr uintptr_t removeItemListEntry = 0x4fadb0;
constexpr uint32_t removeItemListEntryClaim = 6;  // push ebp; mov ebp,esp; and esp,-8
constexpr uintptr_t scriptRemoveCall = 0x40dd73;  // In command 96
constexpr uint32_t callLength = 5;

// Pointers to the std::map headers of the game's tables (see findMapValue).
constexpr uintptr_t itemListGlobal = 0x9e96a0;  // ItemList row -> {int16 kind, int16 ID}
constexpr uintptr_t itemDataGlobal = 0x9e96ac;  // Item ID -> ItemData
constexpr int16_t itemListKindItem = 6;
constexpr uint32_t itemDataUseMotion = 0x24;  // int16, -1 for items not used from the bag
constexpr int16_t noUseMotion = -1;

uint32_t enabled = toggleOff;
enum InlineAsmConstant : uint32_t { enabledValue = toggleOn };  // Inline asm takes enums, not constexpr
GameImage game;
TweakLog logger("Infinite Items");
uintptr_t afterSlotUpdateAddress;  // Relocated at install, so the hooks only compare and jump
uintptr_t scriptRemoveReturn;
void* originalDecrementCount = nullptr;
void* originalRemoveItemListEntry = nullptr;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov eax,[ebx]; mov edx,[esp+0x14]; lea ecx,[edx+eax]; mov al,[ecx+2]; dec al; test al,al;
    // jg; xor al,al; or esi,-1; mov [ecx],si (the slot's ID); movsx cx,al; mov eax,[ebx];
    // mov [edx+eax+2],cx (the count); mov edi,[ebx]; add edi,edx; mov esi,ebx; call (erase)
    const uint8_t decrements[] = {0x8b, 0x03, 0x8b, 0x54, 0x24, 0x14, 0x8d, 0x0c, 0x02, 0x8a, 0x41, 0x02, 0xfe,
        0xc8, 0x84, 0xc0, 0x7f, 0x08, 0x32, 0xc0, 0x83, 0xce, 0xff, 0x66, 0x89, 0x31, 0x66, 0x0f, 0xbe, 0xc8,
        0x8b, 0x03, 0x66, 0x89, 0x4c, 0x02, 0x02, 0x8b, 0x3b, 0x03, 0xfa, 0x8b, 0xf3};
    // mov eax,[esp+0x1c] (the character), call 0x4f8390 (is it the player)
    const uint8_t readsCharacter[] = {0x8b, 0x44, 0x24, 0x1c};
    // push ebp; mov ebp,esp; and esp,-8; mov eax,[ebp+0x10]
    const uint8_t removePrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x8b, 0x45, 0x10};
    // push eax (character); push 1; push edi (row); call 0x4fadb0; add esp,0xc
    const uint8_t scriptRemoves[] = {0x50, 0x6a, 0x01, 0x57};
    return game.codeIs(0x4fc405, decrements, sizeof(decrements)) && game.callsTo(eraseSlotCall, eraseSlot) &&
           game.codeIs(afterSlotUpdate, readsCharacter, sizeof(readsCharacter)) &&
           game.codeIs(removeItemListEntry, removePrologue, sizeof(removePrologue)) &&
           game.operandIs(0x4fae24, itemListGlobal) && game.operandIs(0x4fae52, itemDataGlobal) &&
           game.codeIs(0x40dd6f, scriptRemoves, sizeof(scriptRemoves)) &&
           game.callsTo(scriptRemoveCall, removeItemListEntry);
}

// The value of key in one of the game's maps, or 0: the same search as the game's 0x403be0.
constexpr uint32_t mapRootOffset = 4;  // In the header
constexpr uint32_t mapLeftOffset = 8;
constexpr uint32_t mapRightOffset = 0xc;
constexpr uint32_t mapKeyOffset = 0x10;
constexpr uint32_t mapValueOffset = 0x14;

uintptr_t readPointer(uintptr_t address) { return *reinterpret_cast<const uintptr_t*>(address); }
int16_t mapKey(uintptr_t node) { return *reinterpret_cast<const int16_t*>(node + mapKeyOffset); }

uintptr_t findMapValue(uintptr_t header, int16_t key) {
    if (!header) return 0;
    uintptr_t found = header;
    for (uintptr_t node = readPointer(header + mapRootOffset); node;) {
        if (mapKey(node) < key) {
            node = readPointer(node + mapRightOffset);
        } else {
            found = node;
            node = readPointer(node + mapLeftOffset);
        }
    }
    if (found == header || key < mapKey(found)) return 0;
    return readPointer(found + mapValueOffset);
}

// Runs once per unit a script takes, which is rare, so a search of the two maps is fine.
bool __cdecl isUsableItemRow(int32_t itemListRow) {
    const auto* listEntry = reinterpret_cast<const int16_t*>(
        findMapValue(game.readUint32(itemListGlobal), static_cast<int16_t>(itemListRow)));
    if (!listEntry || listEntry[0] != itemListKindItem) return false;
    const uintptr_t itemData = findMapValue(game.readUint32(itemDataGlobal), listEntry[1]);
    if (!itemData) return false;
    return *reinterpret_cast<const int16_t*>(itemData + itemDataUseMotion) != noUseMotion;
}

// Jumped to in place of mov al,[ecx+2]; dec al. The character is [esp+0x1c] in 0x4fc150's frame.
// For the player it skips the count, the ID and the erase of the slot, so the slot stays as it
// was; eax, ecx, esi and edi are free there (0x4fc435 reloads eax, the epilogue restores the
// others). Runs only when an item took effect.
__declspec(naked) void decrementCountHook() {
    __asm {
        cmp enabled, enabledValue
        jne keepGameBehavior
        mov eax, [esp + 0x1c]
        test eax, eax
        je keepGameBehavior
        mov eax, [eax + 0xc]  // The character's parameters
        test eax, eax
        je keepGameBehavior
        cmp dword ptr [eax + 4], 0  // ID 0 is the player
        jne keepGameBehavior
        jmp dword ptr [afterSlotUpdateAddress]
    keepGameBehavior:
        jmp dword ptr [originalDecrementCount]
    }
}

// cdecl, so the caller pops the arguments: returning at once leaves the item as it is.
__declspec(naked) void removeItemListEntryHook() {
    __asm {
        cmp enabled, enabledValue
        jne keepGameBehavior
        mov eax, [esp]
        cmp eax, scriptRemoveReturn
        jne keepGameBehavior
        push dword ptr [esp + 4]  // The ItemList row
        call isUsableItemRow
        add esp, 4
        test al, al
        je keepGameBehavior
        ret
    keepGameBehavior:
        jmp dword ptr [originalRemoveItemListEntry]
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the use of items is not the known version", decrementCount);
        return false;
    }
    afterSlotUpdateAddress = game.address(afterSlotUpdate);
    scriptRemoveReturn = game.address(scriptRemoveCall + callLength);

    if (CodeChangeError error = hookGameCode(context, game, decrementCount, decrementCountLength,
            reinterpret_cast<void*>(&decrementCountHook), &originalDecrementCount)) {
        logger.installFailed(error, decrementCount);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, removeItemListEntry, removeItemListEntryClaim,
            reinterpret_cast<void*>(&removeItemListEntryHook), &originalRemoveItemListEntry)) {
        unhookGameCode(game, decrementCount);
        logger.installFailed(error, removeItemListEntry);
        return false;
    }
    return true;
}

const Option options[] = {
    toggle(L"enabled", {L"アイテム減らない", L"Infinite Items"}, &enabled, false),
};

}  // namespace

const Tweak infiniteItemsTweak{L"infinite_items", options, 1, install};

}  // namespace tweaks
