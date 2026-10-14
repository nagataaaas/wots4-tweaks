// Installs mod.dll on a copy of the exe mapped into this process and runs the ownership check of
// the accessory screen's list (0x5e442f) with a synthetic game state and accessory data. The two
// places the check goes to are replaced by stubs that tell which one was reached; Steam's answer
// for a DLC the player does not have comes from a stand-in.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t listLoopStart = 0x5e442f;  // mov eax,[esi+0x40]; cmp byte [eax+edi+0x391a],0
constexpr uintptr_t ownedCheck = 0x5e4432;
constexpr uintptr_t listOwned = 0x5e4440;
constexpr uintptr_t nextId = 0x5e4690;
constexpr uintptr_t accessoryDataGlobal = 0x9e95c4;
constexpr uintptr_t steamAppsImport = 0x905568;
constexpr uint32_t screenGameState = 0x40;
constexpr uint32_t gameStateOwned = 0x391a;
constexpr uint32_t gameStateDlcOwned = 0x493b4;  // The game's cache of Steam's answers
constexpr uint32_t dataDlc = 0x2e;
constexpr uint32_t reachedList = 1;
constexpr uint32_t reachedNextId = 2;

constexpr int16_t baseAccessory = 5;      // DLC -1
constexpr int16_t ownedDlcAccessory = 152;  // DLC 0, which the player has
constexpr int16_t missingDlcAccessory = 155;  // DLC 2, which the player does not have
constexpr int16_t idWithoutData = 170;

struct TreeNode {
    TreeNode* parent;
    uint32_t unused;
    TreeNode* left;    // +8
    TreeNode* right;   // +0xc
    int16_t id;        // +0x10
    uint16_t padding;
    uint8_t* data;     // +0x14
};

alignas(4) uint8_t gameState[gameStateDlcOwned + 0x20];
alignas(4) uint8_t screen[0x100];
alignas(4) uint8_t accessoryManager[0x20];
alignas(4) uint8_t baseData[0x40], ownedDlcData[0x40], missingDlcData[0x40];
TreeNode baseNode, ownedDlcNode, missingDlcNode;

void setDlc(uint8_t* data, int16_t dlc) { *reinterpret_cast<int16_t*>(data + dataDlc) = dlc; }

void buildAccessoryData() {
    setDlc(baseData, -1);
    setDlc(ownedDlcData, 0);
    setDlc(missingDlcData, 2);
    baseNode = {nullptr, 0, nullptr, nullptr, baseAccessory, 0, baseData};
    missingDlcNode = {nullptr, 0, nullptr, nullptr, missingDlcAccessory, 0, missingDlcData};
    ownedDlcNode = {nullptr, 0, &baseNode, &missingDlcNode, ownedDlcAccessory, 0, ownedDlcData};
    *reinterpret_cast<TreeNode**>(accessoryManager + 8) = &ownedDlcNode;  // The root, at manager +4 +4
    *reinterpret_cast<uintptr_t*>(va(accessoryDataGlobal)) = reinterpret_cast<uintptr_t>(accessoryManager);
    gameState[gameStateDlcOwned + 0] = 1;
}

// ISteamApps with BIsSubscribedApp (slot 6) answering no.
bool __fastcall notSubscribed(void*, void*, uint32_t) { return false; }
void* fakeSteamAppsTable[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    reinterpret_cast<void*>(&notSubscribed), nullptr};
void* fakeSteamApps[1] = {fakeSteamAppsTable};
void* __cdecl steamApps() { return fakeSteamApps; }

// mov eax,<value>; ret
void returnValueAt(uintptr_t preferred, uint32_t value) {
    uint8_t* code = &codeByte(preferred);
    code[0] = 0xb8;
    std::memcpy(code + 1, &value, sizeof(value));
    code[5] = returnOpcode;
}

uint32_t runCheck(int16_t id, bool owned) {
    gameState[gameStateOwned + id] = owned ? 1 : 0;
    const uintptr_t function = va(listLoopStart);
    uint8_t* screenAddress = screen;
    uint32_t reached;
    const uint32_t accessoryId = static_cast<uint32_t>(id);
    __asm {
        push ebx
        push esi
        push edi
        mov esi, screenAddress
        mov edi, accessoryId
        call function
        mov reached, eax
        pop edi
        pop esi
        pop ebx
    }
    gameState[gameStateOwned + id] = 0;
    return reached;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_accessory_all")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or hooked.
    codeByte(ownedCheck + 3) = 0x1b;  // the owned flags' offset 0x391a -> 0x391b
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("All Accessories: the accessory screen is not the known version at 0x5e4432; the tweak is "
                     "skipped") &&
              loggedText("Tweak accessory_all could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(ownedCheck) && codeByte(ownedCheck) == 0x80 && optionIndex(L"All Accessories") == noItem,
        "nothing is claimed, hooked or shown");
    codeByte(ownedCheck + 3) = 0x1a;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(ownedCheck, 14) && codeByte(ownedCheck) == jumpOpcode,
        "the ownership check is hooked (14 bytes claimed: the cmp and the je)");
    const uint32_t item = optionIndex(L"All Accessories");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"全装飾品使用可",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    returnValueAt(listOwned, reachedList);
    returnValueAt(nextId, reachedNextId);
    *reinterpret_cast<uint8_t**>(screen + screenGameState) = gameState;
    *reinterpret_cast<uintptr_t*>(va(steamAppsImport)) = reinterpret_cast<uintptr_t>(&steamApps);
    buildAccessoryData();

    check(runCheck(baseAccessory, true) == reachedList && runCheck(baseAccessory, false) == reachedNextId,
        "off: only owned accessories are listed");

    changeOption(item, 1);
    check(runCheck(baseAccessory, false) == reachedList, "on: an accessory of the base game is listed unowned");
    check(gameState[gameStateOwned + baseAccessory] == 0, "on: the owned flag is not written");
    check(runCheck(ownedDlcAccessory, false) == reachedList, "on: an accessory of a DLC the player has is listed");
    check(runCheck(missingDlcAccessory, false) == reachedNextId,
        "on: an accessory of a DLC the player does not have is not listed");
    check(runCheck(missingDlcAccessory, true) == reachedList, "on: but it is when owned, as the game has it");
    check(runCheck(idWithoutData, false) == reachedNextId, "on: an ID without accessory data is not listed");
    *reinterpret_cast<uintptr_t*>(va(accessoryDataGlobal)) = 0;
    check(runCheck(baseAccessory, false) == reachedNextId, "on: without the accessory data nothing more is listed");
    *reinterpret_cast<uintptr_t*>(va(accessoryDataGlobal)) = reinterpret_cast<uintptr_t>(accessoryManager);

    changeOption(item, 0);
    check(runCheck(baseAccessory, false) == reachedNextId, "off again: unowned accessories are not listed");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"accessory_all", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [accessory_all] enabled=1");

    return finish("accessory_all_native_probe");
}
