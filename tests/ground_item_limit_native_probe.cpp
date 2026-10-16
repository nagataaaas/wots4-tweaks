// Installs mod.dll on a copy of the exe mapped into this process with config.ini asking for the
// Dropped Item Limit of 120 (240 places on the ground), then runs the game's own code on a synthetic
// object of the new size: the constructor (0x4f5b60) and the search for a free place (0x4f7710),
// which go through the tweak's stubs. Also checks the operands written, the limit and the restart
// notice on the page. battle_tweaks_native_probe checks a start with the default.
#include <cstdio>
#include <cstring>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uint32_t placeCount = 240;
constexpr uint32_t placeSize = 0x1c;
constexpr uint32_t listOffset = 8 + placeCount * placeSize;  // 0x1a48
constexpr uint32_t objectSize = listOffset + 8;              // 0x1a50
constexpr uint32_t guardSize = 0x40;
constexpr uint8_t guardByte = 0xcc;
constexpr uintptr_t constructor = 0x4f5b60;      // ebx = object
constexpr uintptr_t findFreePlace = 0x4f7710;    // edx = object, esi = where to write the index
constexpr uintptr_t markPlaces = 0x4f7130;       // eax = object, ecx = key; marks the taken places with it
constexpr uintptr_t spawnerVtable = 0x988540;
constexpr uintptr_t limitOperand = 0x4f7796;     // mov cl, 30

uint32_t codeUint32(uintptr_t preferred) {
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const void*>(va(preferred)), sizeof(value));
    return value;
}

void runConstructor(uint8_t* object) {
    const uintptr_t function = va(constructor);
    __asm {
        push ebx
        mov ebx, object
        call function
        pop ebx
    }
}

uint8_t* runFindFreePlace(uint8_t* object, uint32_t* index) {
    const uintptr_t function = va(findFreePlace);
    uint8_t* place;
    __asm {
        push esi
        mov edx, object
        mov esi, index
        call function
        mov place, eax
        pop esi
    }
    return place;
}

void runMarkPlaces(uint8_t* object, uint32_t key) {
    const uintptr_t function = va(markPlaces);
    __asm {
        mov eax, object
        mov ecx, key
        call function
    }
}

uint32_t& at(uint8_t* object, uint32_t offset) { return *reinterpret_cast<uint32_t*>(object + offset); }

void checkOperands() {
    check(codeUint32(0x4f7c1c) == objectSize && codeUint32(0x553a1f) == objectSize &&
              codeUint32(0x4f8244) == objectSize && codeUint32(0x4f5bf2) == objectSize,
        "the object is made and freed with 0x1a50 bytes");
    const uintptr_t listOperands[] = {0x4f5b82, 0x4f5cbb, 0x4f5cc1, 0x4f6634, 0x4f6f12, 0x4f6f18, 0x4f732b, 0x4f7331,
        0x4f7639};
    bool listMoved = true;
    for (uintptr_t operand : listOperands) listMoved = listMoved && codeUint32(operand) == listOffset;
    check(listMoved, "all 9 uses of the list head go to +0x1a48");
    check(codeUint32(0x4f5b91) == placeCount && codeUint32(0x4f6ea7) == placeCount, "the 32-bit counts are 240");
    check(codeUint32(0x4f7791) == placeCount / 5 && codeUint32(0x4f7132) == placeCount / 6 &&
              codeByte(0x4f6f6b) == placeCount / 6,
        "the loops of 5 and 6 places a turn go 48 and 40 turns");
    check(codeByte(0x4f713b) == static_cast<uint8_t>(1 - placeCount / 6),
        "the loop at 0x4f7130 still steps by 1 (lea edx, [esi-39])");
    const uintptr_t stubbed[] = {0x4f5ca5, 0x4f6a59, 0x4f70ec, 0x4f71ba, 0x4f721d, 0x4f7266, 0x4f771e, 0x4f7909,
        0x4f7992, 0x4fcffe, 0x522bb9, 0x4f5b6d, 0x4f5d44};
    bool jumps = true;
    for (uintptr_t site : stubbed) jumps = jumps && codeByte(site) == jumpOpcode && claimedAt(site);
    check(jumps, "the 13 8-bit checks and counts jump to stubs and are claimed");
    check(codeByte(0x4f70ec + 5) == 0x90 && codeByte(0x4f70ec + 8) == 0x90, "the near jl's rest is nops");
}

void checkGameCodeOnBiggerObject() {
    std::vector<uint8_t> memory(objectSize + guardSize, guardByte);
    uint8_t* object = memory.data();
    runConstructor(object);
    check(at(object, 0) == va(spawnerVtable), "the game's constructor runs on the 0x1a50-byte object");
    bool placesCleared = true;
    for (uint32_t place = 0; place < placeCount; ++place) {
        const uint32_t start = 8 + place * placeSize;
        placesCleared = placesCleared && at(object, start) == 0 && at(object, start + 4) == 0 &&
                        object[start + 0x18] == 0;
    }
    check(placesCleared, "all 240 places are cleared (array constructor and the clearing loop)");
    const auto listHead = reinterpret_cast<uint32_t>(object + listOffset);
    check(at(object, listOffset) == listHead && at(object, listOffset + 4) == listHead,
        "the list head behind the places points to itself");
    bool guardKept = true;
    for (uint32_t offset = objectSize; offset < objectSize + guardSize; ++offset)
        guardKept = guardKept && object[offset] == guardByte;
    check(guardKept, "nothing is written past the object");

    for (uint32_t place = 0; place < 200; ++place) at(object, 8 + place * placeSize) = 1;
    uint32_t index = 0;
    uint8_t* found = runFindFreePlace(object, &index);
    check(index == 200 && found == object + 8 + 200 * placeSize, "the search finds place 200 (past 60 and 127)");
    for (uint32_t place = 200; place < placeCount; ++place) at(object, 8 + place * placeSize) = 1;
    index = 99;
    found = runFindFreePlace(object, &index);
    check(found == nullptr && index == 99, "with all 240 places taken the search finds none and stops");

    constexpr uint32_t key = 0x1234;
    constexpr uint32_t markOffset = 0x18;
    const uint32_t keyedPlaces[] = {0, 59, 60, 200, placeCount - 1};
    for (uint32_t place = 0; place < placeCount; ++place) {
        at(object, 8 + place * placeSize + 4) = 0;
        object[8 + place * placeSize + markOffset] = 0;
    }
    for (uint32_t place : keyedPlaces) at(object, 8 + place * placeSize + 4) = key;
    runMarkPlaces(object, key);
    bool marksRight = true;
    for (uint32_t place = 0; place < placeCount; ++place) {
        bool keyed = false;
        for (uint32_t keyedPlace : keyedPlaces) keyed = keyed || place == keyedPlace;
        marksRight = marksRight && object[8 + place * placeSize + markOffset] == (keyed ? 1 : 0);
    }
    check(marksRight, "0x4f7130 marks with 1 just the places 0, 59, 60, 200 and 239 holding the key, and returns");
    guardKept = true;
    for (uint32_t offset = objectSize; offset < objectSize + guardSize; ++offset)
        guardKept = guardKept && object[offset] == guardByte;
    check(guardKept, "0x4f7130 writes nothing past the object");
}

void checkRestartNotice() {
    const uint32_t item = optionIndex(L"Dropped Item Limit");
    check(item != noItem && optionItem(item).kind == wots4::optionChoice && optionItem(item).choiceCount == 3 &&
              std::wstring(optionItem(item).label.japanese) == L"ドロップ品の上限",
        "Dropped Item Limit (ドロップ品の上限): a choice of 3");
    if (item == noItem) return;
    check(optionValue(item) == 2, "the value from config.ini is shown");
    check(codeByte(limitOperand) == 120, "the limit of each kind is 120 (half the places)");
    const wots4::OptionItem& notice = pages.back().items[item + 1];
    check(notice.kind == wots4::optionNotice && notice.defaultValue == item && notice.choiceCount == 0b011 &&
              std::wstring(notice.label.japanese) == L"変更はゲームを再起動した後にすべて有効になります",
        "the restart notice follows it and shows for the values other than the startup one");
    changeOption(item, 0);
    check(codeByte(limitOperand) == 30, "a change in the game sets the limit at once");
    check(codeUint32(0x4f7c1c) == objectSize, "but does not write the places' code again");
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_ground_item_limit")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;
    WritePrivateProfileStringW(L"ground_item_limit", L"limit", L"2", configPath().c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, configPath().c_str());

    check(initializeMod(), "the Mod initializes with [ground_item_limit] limit=2");
    check(loggedText("Dropped Item Limit: the ground has 240 places for things"), "the 240 places are logged");
    checkOperands();
    checkGameCodeOnBiggerObject();
    checkRestartNotice();
    return finish("ground_item_limit_native_probe");
}
