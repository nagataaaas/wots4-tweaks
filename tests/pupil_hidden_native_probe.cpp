// Installs mod.dll on a copy of the exe mapped into this process and calls the two hooked places
// with a synthetic game state. The game's code behind them needs the running game, so on the
// copy it is replaced by stubs: 0x5ac7d0 returns 0x1234 (a pupil's record) and the area
// check 0x509980 returns 5 (an area where the game would send a messenger).
#include <cstdio>
#include <cstring>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t findPupilToAppear = 0x5ac7d0;
constexpr uintptr_t afterMovedInstructions = 0x5ac7d9;  // push ebp; mov ebp,esp; mov edx,[0x9ecbb4]
constexpr uintptr_t messengerAreaCheck = 0x5ac2fb;
constexpr uintptr_t afterMessengerAreaCheck = 0x5ac300;
constexpr uintptr_t areasWithoutMessenger = 0x509980;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uint32_t pupilRecord = 0x1234;
constexpr uint8_t gameAreaAnswer = 5;
constexpr uint8_t dojoArea = 8, townArea = 3;

// mov esp,ebp; pop ebp; mov eax,0x1234; ret 4
const uint8_t findPupilStub[] = {0x8b, 0xe5, 0x5d, 0xb8, 0x34, 0x12, 0x00, 0x00, 0xc2, 0x04, 0x00};
// mov al,5; ret
const uint8_t areaCheckStub[] = {0xb0, gameAreaAnswer, 0xc3};

uint32_t findPupil(int definition) {
    const uintptr_t function = va(findPupilToAppear);
    uint32_t record;
    __asm {
        push definition
        call function
        mov record, eax
    }
    return record;
}

uint8_t checkMessengerArea(uint32_t area) {
    const uintptr_t function = va(messengerAreaCheck);
    uint8_t answer;
    __asm {
        mov eax, area
        call function
        mov answer, al
    }
    return answer;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_pupil_hidden")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    codeByte(0x5ac8c9) = 0x07;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Pupils Stay in the Dojo: the placement of pupils is not the known version at 0x5ac7d0; the "
                     "tweak is skipped") &&
              loggedText("Tweak pupil_hidden could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(findPupilToAppear) && !claimedAt(messengerAreaCheck) &&
              optionIndex(L"Pupils Stay in the Dojo") == noItem && codeByte(findPupilToAppear) == 0x55,
        "nothing is claimed, hooked or shown");
    codeByte(0x5ac8c9) = 0x08;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(findPupilToAppear, 5) && claimed(messengerAreaCheck, 5),
        "0x5ac7d0 and 0x5ac2fb are claimed (5 bytes each)");
    const uint32_t item = optionIndex(L"Pupils Stay in the Dojo");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"弟子がマップに出ない",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    std::memcpy(reinterpret_cast<void*>(va(afterMovedInstructions)), findPupilStub, sizeof(findPupilStub));
    std::memcpy(reinterpret_cast<void*>(va(areasWithoutMessenger)), areaCheckStub, sizeof(areaCheckStub));
    codeByte(afterMessengerAreaCheck) = returnOpcode;
    std::vector<uint8_t> gameState(0x48c00);
    *reinterpret_cast<uint8_t**>(va(gameStateGlobal)) = gameState.data();
    gameState[0x48b9a] = townArea;

    check(findPupil(42) == pupilRecord, "off: in town the game decides whether a pupil appears");
    check(checkMessengerArea(townArea) == gameAreaAnswer, "off: the game decides whether a messenger comes");

    changeOption(item, 1);
    check(findPupil(42) == 0, "on: in town a townsperson appears as they are, never as a pupil");
    check(checkMessengerArea(townArea) == 1, "on: every area counts as one without a messenger");
    gameState[0x48b9a] = dojoArea;
    check(findPupil(42) == pupilRecord, "on: at the dojo the pupils appear as in the game");

    changeOption(item, 0);
    gameState[0x48b9a] = townArea;
    check(findPupil(42) == pupilRecord && checkMessengerArea(townArea) == gameAreaAnswer,
        "off again: the game decides");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"pupil_hidden", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [pupil_hidden] enabled=1");

    return finish("pupil_hidden_native_probe");
}
