// Installs mod.dll on a copy of the exe mapped into this process and checks the extra respawning
// enemies of a mission: which calls of 0x436370 ask for them, which definitions they use, and how
// each made one is given to a synthetic respawning-enemies manager on a later frame. The game's
// functions the tweak calls are replaced in the copy by stubs that count their calls.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t setUpRespawn = 0x436370;
constexpr uintptr_t setUpRespawnBody = 0x436376;  // Where the hook's trampoline goes on
constexpr uintptr_t missionSetUpReturn = 0x50cf1b;
constexpr uintptr_t requestCharacterCode = 0x43a990;
constexpr uintptr_t putBackCode = 0x437150;
constexpr uintptr_t growAndAppendCode = 0x437750;
constexpr uintptr_t managersGlobal = 0x9e9640;
constexpr uintptr_t characterManagerGlobal = 0x9e9658;
constexpr uintptr_t missionManagerGlobal = 0x9e96c0;
constexpr int32_t firstNewId = 0x300;
constexpr uint32_t missionEnemies = 8;
constexpr uint32_t gameWait = 30;

struct RespawnEntry {
    uintptr_t character;
    int32_t waitFrames;
    uint8_t out;
    uint8_t padding[3];
};
struct CharacterNode {
    uintptr_t links[2];
    CharacterNode* left;
    CharacterNode* right;
    int32_t id;
    uintptr_t character;
};
struct Character {
    uint8_t before[0xc];
    uint8_t* parameters;
    uint32_t state;
};

// What the stubs record.
uint32_t requestCount, putBackCount, growCount, originalCount;
int32_t requestedDefinitions[64];

// The synthetic game.
uint32_t managers[4];
uint8_t respawn[0x60];
RespawnEntry listStorage[missionEnemies + 1];
uint32_t characterManager[3];
CharacterNode nodes[16];
Character characters[16];
uint8_t parameters[16][0x300];
uint32_t missionManager[4];
uint8_t missionRow[0x4c];
int32_t missionIds[missionEnemies];

uintptr_t hookedSetUp, missionReturn;

template <typename T>
void writeAt(uint8_t*& code, T value) {
    std::memcpy(code, &value, sizeof(value));
    code += sizeof(value);
}
void writeBytes(uint8_t*& code, std::initializer_list<uint8_t> bytes) {
    for (uint8_t byte : bytes) *code++ = byte;
}
uint32_t addressOf(const void* pointer) { return reinterpret_cast<uint32_t>(pointer); }

void writeStubs() {
    // 0x43a990: requestedDefinitions[n] = ecx; return firstNewId + n++  (ret 0xc)
    uint8_t* code = reinterpret_cast<uint8_t*>(va(requestCharacterCode));
    writeBytes(code, {0xa1});
    writeAt(code, addressOf(&requestCount));
    writeBytes(code, {0x89, 0x0c, 0x85});
    writeAt(code, addressOf(requestedDefinitions));
    writeBytes(code, {0xff, 0x05});
    writeAt(code, addressOf(&requestCount));
    writeBytes(code, {0x05});
    writeAt(code, firstNewId);
    writeBytes(code, {0xc2, 0x0c, 0x00});
    // 0x437150 and 0x437750: count the call (ret 4)
    code = reinterpret_cast<uint8_t*>(va(putBackCode));
    writeBytes(code, {0xff, 0x05});
    writeAt(code, addressOf(&putBackCount));
    writeBytes(code, {0xc2, 0x04, 0x00});
    code = reinterpret_cast<uint8_t*>(va(growAndAppendCode));
    writeBytes(code, {0xff, 0x05});
    writeAt(code, addressOf(&growCount));
    writeBytes(code, {0xc2, 0x04, 0x00});
    // The body of 0x436370 after the hooked prologue: originalCount = count; return 0 (ret 0x14)
    code = reinterpret_cast<uint8_t*>(va(setUpRespawnBody));
    writeBytes(code, {0x8b, 0x45, 0x10, 0xa3});
    writeAt(code, addressOf(&originalCount));
    writeBytes(code, {0x8b, 0xe5, 0x5d, 0x31, 0xc0, 0xc2, 0x14, 0x00});
    // The mission's call site returns straight to whoever called callAsMission.
    code = reinterpret_cast<uint8_t*>(va(missionSetUpReturn));
    writeBytes(code, {0xc2, 0x14, 0x00});
}

// Calls the hooked 0x436370 as the mission's update does, with 0x50cf1b as the return address.
__declspec(naked) uint32_t __stdcall callAsMission(uint8_t*, const int32_t*, int32_t, int32_t, uint32_t) {
    __asm {
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push dword ptr [esp + 20]
        push missionReturn
        jmp hookedSetUp
    }
}

using SetUpRespawn = uint32_t(__stdcall*)(uint8_t*, const int32_t*, int32_t, int32_t, uint32_t);

void resetGame() {
    std::memset(respawn, 0, sizeof(respawn));
    std::memset(listStorage, 0, sizeof(listStorage));
    for (uint32_t index = 0; index < missionEnemies; ++index) {
        listStorage[index].character = 0x1000 + index;
        listStorage[index].waitFrames = gameWait;
    }
    auto** list = reinterpret_cast<RespawnEntry**>(respawn + 0x14);
    list[0] = listStorage;
    list[1] = listStorage + missionEnemies;
    list[2] = listStorage + missionEnemies + 1;  // Room for one more before 0x437750 must grow it
    *reinterpret_cast<uint32_t*>(respawn + 0x3c) = 1;
    managers[2] = addressOf(respawn);
    *reinterpret_cast<uint32_t*>(va(managersGlobal)) = addressOf(managers);

    std::memset(nodes, 0, sizeof(nodes));
    std::memset(characters, 0, sizeof(characters));
    std::memset(parameters, 0, sizeof(parameters));
    characterManager[2] = 0;  // No character made yet
    *reinterpret_cast<uint32_t*>(va(characterManagerGlobal)) = addressOf(characterManager);

    const int16_t definitions[8] = {760, 761, 762, -1, -1, -1, -1, -1};
    std::memcpy(missionRow + 0x2c, definitions, sizeof(definitions));
    missionManager[3] = addressOf(missionRow);
    *reinterpret_cast<uint32_t*>(va(missionManagerGlobal)) = addressOf(missionManager);

    // The mission's own enemies; the last one has the ID the first request returns (a fixed ID).
    for (uint32_t index = 0; index < missionEnemies; ++index) missionIds[index] = 0x100 + index;
    missionIds[missionEnemies - 1] = firstNewId;
    requestCount = putBackCount = growCount = originalCount = 0;
}

// Makes the characters of the IDs the stub gave (firstNewId + n), ready or still loading.
void makeCharacters(uint32_t count, uint32_t loadingIndex) {
    for (uint32_t index = 0; index < count; ++index) {
        nodes[index].id = firstNewId + 1 + index;
        nodes[index].character = addressOf(&characters[index]);
        nodes[index].right = index + 1 < count ? &nodes[index + 1] : nullptr;
        characters[index].parameters = parameters[index];
        characters[index].state = index == loadingIndex ? 1 : 3;
    }
    characterManager[2] = addressOf(&nodes[0]);
}

uint32_t listLength() {
    auto** list = reinterpret_cast<RespawnEntry**>(respawn + 0x14);
    return static_cast<uint32_t>(list[1] - list[0]);
}

uint32_t markedCharacters() {
    uint32_t count = 0;
    for (auto& parameter : parameters) count += (*reinterpret_cast<uint32_t*>(parameter + 0x2fc) & 0x10) ? 1 : 0;
    return count;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_enemy_pool_size")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed.
    codeByte(0x50cf0d) = 0x6b;  // push 0 -> another opcode before the mission's call
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Enemy Pool Size: the respawning enemies of missions are not the known version at 0x436370; "
                     "the tweak is skipped"),
        "the skipped tweak is logged");
    check(!claimedAt(setUpRespawn) && optionIndex(L"Enemy Pool Size") == noItem, "nothing is claimed or shown");
    codeByte(0x50cf0d) = 0x6a;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(setUpRespawn, 6) && codeByte(setUpRespawn) == jumpOpcode, "0x436370 is hooked (6 bytes claimed)");
    check(!frameHandlers.empty(), "a per-frame callback is registered");
    const uint32_t item = optionIndex(L"Enemy Pool Size");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"雑魚の候補数",
        "a choice of 3 is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the game's 8 enemies are kept");

    hookedSetUp = va(setUpRespawn);
    missionReturn = va(missionSetUpReturn);
    writeStubs();
    resetGame();

    callAsMission(respawn, missionIds, missionEnemies, 25, 0);
    check(originalCount == missionEnemies, "default: the game's 0x436370 runs with the mission's 8 enemies");
    check(requestCount == 0, "default: no enemy is asked for");

    changeOption(item, 2);
    reinterpret_cast<SetUpRespawn>(hookedSetUp)(respawn, missionIds, missionEnemies, -1, 1);
    check(originalCount == missionEnemies && requestCount == 0,
        "24: a battle script's call (another return address) asks for no enemy");

    callAsMission(respawn, missionIds, missionEnemies, 25, 0);
    check(requestCount == 16, "24: a mission asks for 16 more (24 - 8)");
    bool cycled = true;
    const int32_t expected[3] = {760, 761, 762};
    for (uint32_t index = 0; index < requestCount; ++index) cycled &= requestedDefinitions[index] == expected[index % 3];
    check(cycled, "the extra enemies use the row's definitions in turn (760, 761, 762, ...)");

    runFrame();
    check(listLength() == missionEnemies && putBackCount == 0, "before they are made, nothing is given");

    // The first request returned the ID of one of the mission's own enemies, so 15 are waited for.
    makeCharacters(15, 14);
    runFrame();
    check(putBackCount == 14, "the 14 made characters are hidden as waiting enemies (0x437150)");
    check(listLength() == missionEnemies + 1 && growCount == 13,
        "one fits in the list; for the rest the game's 0x437750 grows it");
    const RespawnEntry& added = listStorage[missionEnemies];
    check(added.character == addressOf(&characters[0]) && added.waitFrames == gameWait && added.out == 0,
        "an added entry is the character, the game's first wait, not out");
    check(markedCharacters() == 14, "the given characters are marked as respawning (+0x2fc |= 0x10)");

    characters[14].state = 3;
    runFrame();
    check(putBackCount == 15 && markedCharacters() == 15, "the one still loading is given once it is ready");
    runFrame();
    check(putBackCount == 15, "each is given once");

    // Stopped before the enemies are made: they are left as the game's other characters.
    resetGame();
    callAsMission(respawn, missionIds, missionEnemies, 25, 0);
    *reinterpret_cast<uint32_t*>(respawn + 0x3c) = 0;
    runFrame();
    makeCharacters(15, 99);
    *reinterpret_cast<uint32_t*>(respawn + 0x3c) = 1;
    runFrame();
    check(putBackCount == 0 && listLength() == missionEnemies, "after the respawning stopped, nothing is given");

    // Never made: given up after 300 frames with one warning.
    resetGame();
    callAsMission(respawn, missionIds, missionEnemies, 25, 0);
    for (int frame = 0; frame < 301; ++frame) runFrame();
    check(loggedText("Enemy Pool Size: 15 extra enemies were not made within 300 frames"),
        "enemies not made in 300 frames are given up with a warning");
    makeCharacters(15, 99);
    runFrame();
    check(putBackCount == 0, "given-up enemies are not given later");

    changeOption(item, 1);
    resetGame();
    callAsMission(respawn, missionIds, missionEnemies, 25, 0);
    check(requestCount == 8, "16: a mission asks for 8 more");

    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"enemy_pool_size", L"pool", 99, configPath().c_str()) == 1,
        "the options save writes [enemy_pool_size] pool=1");

    return finish("enemy_pool_size_native_probe");
}
