// Owning an accessory is one byte per ID at game state +0x391a (unlocks and DLC set it too). The
// accessory screen (EquipAccessory.cpp, 0x5e4000) lists IDs 0 to 199 for which that byte is set;
// the hook lists the others as well. Shops, the records screen and gifts read the byte elsewhere
// and are left as they are, and the byte is never written.
#include "accessory_all.h"
#include "../game_code.h"

namespace tweaks {

namespace {

// cmp byte [eax+edi+0x391a],0 (8 bytes), then je 0x5e4690 (next ID); eax = game state, edi = ID.
constexpr uintptr_t ownedCheck = 0x5e4432;
constexpr uint32_t ownedCheckClaim = 14;  // The cmp and the je
constexpr uintptr_t listOwned = 0x5e4440;  // Where an owned accessory goes
constexpr uintptr_t accessoryDataGlobal = 0x9e95c4;
// The game's std::map from ID to accessory data at manager +4, walked as 0x5e4440 does.
constexpr uint32_t managerTree = 4;
constexpr uint32_t treeRoot = 4;
constexpr uint32_t nodeLeft = 8;
constexpr uint32_t nodeRight = 0xc;
constexpr uint32_t nodeId = 0x10;  // int16
constexpr uint32_t nodeData = 0x14;
constexpr uint32_t dataDlc = 0x2e;  // int16; -1 for the base game
constexpr int16_t baseGame = -1;
// esi = DLC number, ebx = game state; returns al. Asks Steam whether the DLC is owned.
constexpr uintptr_t dlcOwned = 0x4dc890;

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("All Accessories");
const uintptr_t* accessoryData;  // Relocated at install
uintptr_t listOwnedAddress;
uintptr_t dlcOwnedAddress;
void* originalOwnedCheck;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov eax,[esi+0x40]; cmp byte [eax+edi+0x391a],0; je 0x5e4690; mov edx,[esp+0x50] (the manager)
    const uint8_t ownedLoop[] = {0x8b, 0x46, 0x40, 0x80, 0xbc, 0x38, 0x1a, 0x39, 0x00, 0x00, 0x00, 0x0f, 0x84, 0x50,
        0x02, 0x00, 0x00, 0x8b, 0x54, 0x24, 0x50};
    // mov eax,[edx+8]; add edx,4; mov ecx,edx; test eax,eax; je; cmp word [eax+0x10],di
    const uint8_t findData[] = {0x8b, 0x42, 0x08, 0x83, 0xc2, 0x04, 0x8b, 0xca, 0x85, 0xc0, 0x74, 0x20, 0x66, 0x39,
        0x78, 0x10};
    const uint8_t nextId[] = {0x47, 0x89, 0x7c, 0x24, 0x1c, 0x81, 0xff, 0xc8, 0x00, 0x00, 0x00};  // inc edi; ...; cmp edi,200
    // cmp esi,0x18; jge; cmp byte [esi+ebx+0x493b4],0
    const uint8_t dlcCheck[] = {0x83, 0xfe, 0x18, 0x7d, 0x36, 0x80, 0xbc, 0x1e, 0xb4, 0x93, 0x04, 0x00, 0x00};
    const uint8_t loadManager[] = {0xa1};  // mov eax,[0x9e95c4] ... mov [esp+0x5c],eax
    return game.codeIs(0x5e442f, ownedLoop, sizeof(ownedLoop)) && game.codeIs(0x5e4444, findData, sizeof(findData)) &&
           game.codeIs(0x5e4690, nextId, sizeof(nextId)) && game.codeIs(dlcOwned, dlcCheck, sizeof(dlcCheck)) &&
           game.codeIs(0x5e40a2, loadManager, sizeof(loadManager)) && game.operandIs(0x5e40a3, accessoryDataGlobal);
}

const uint8_t* findAccessoryData(int16_t id) {
    const auto manager = static_cast<uintptr_t>(*accessoryData);
    if (!manager) return nullptr;
    const uintptr_t tree = manager + managerTree;
    auto field = [](uintptr_t node, uint32_t offset) { return *reinterpret_cast<const uintptr_t*>(node + offset); };
    uintptr_t node = field(tree, treeRoot);
    uintptr_t lowerBound = tree;
    while (node) {
        if (*reinterpret_cast<const int16_t*>(node + nodeId) >= id) {
            lowerBound = node;
            node = field(node, nodeLeft);
        } else {
            node = field(node, nodeRight);
        }
    }
    if (lowerBound == tree || *reinterpret_cast<const int16_t*>(lowerBound + nodeId) != id) return nullptr;
    return reinterpret_cast<const uint8_t*>(field(lowerBound, nodeData));
}

bool playerOwnsDlc(int32_t dlc, uintptr_t gameState) {
    const uintptr_t function = dlcOwnedAddress;
    bool owned;
    __asm {
        push ebx
        push esi
        push edi
        mov esi, dlc
        mov ebx, gameState
        call function
        mov owned, al
        pop edi
        pop esi
        pop ebx
    }
    return owned;
}

// Called for each ID when the screen builds its list, not each frame. Only IDs that have data
// are listed: the game reads the data of every listed ID without checking it.
bool __cdecl listsAsOwned(uint32_t id, uintptr_t gameState) {
    if (enabled != toggleOn) return false;
    const uint8_t* data = findAccessoryData(static_cast<int16_t>(id));
    if (!data) return false;
    const int16_t dlc = *reinterpret_cast<const int16_t*>(data + dataDlc);
    return dlc == baseGame || playerOwnsDlc(dlc, gameState);
}

__declspec(naked) void ownedCheckHook() {
    __asm {
        pushad
        push eax  // The game state
        push edi  // The ID
        call listsAsOwned
        add esp, 8
        test al, al
        popad  // Keeps the flags
        jnz listed
        jmp dword ptr [originalOwnedCheck]
    listed:
        jmp dword ptr [listOwnedAddress]
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the accessory screen is not the known version", ownedCheck);
        return false;
    }
    accessoryData = reinterpret_cast<const uintptr_t*>(game.address(accessoryDataGlobal));
    listOwnedAddress = game.address(listOwned);
    dlcOwnedAddress = game.address(dlcOwned);
    if (CodeChangeError error = hookGameCode(context, game, ownedCheck, ownedCheckClaim,
            reinterpret_cast<void*>(&ownedCheckHook), &originalOwnedCheck)) {
        logger.installFailed(error, ownedCheck);
        return false;
    }
    return true;
}

// 装飾品 is "Accessories" in the game's menus. The list is built when the screen opens.
const Option options[] = {
    toggle(L"enabled", {L"全装飾品使用可", L"All Accessories"}, &enabled, false),
};

}  // namespace

const Tweak accessoryAllTweak{L"accessory_all", options, 1, install};

}  // namespace tweaks
