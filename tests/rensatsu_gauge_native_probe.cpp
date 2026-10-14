// Installs mod.dll on a copy of the exe mapped into this process and runs the Spring Harvest
// update 0x43cc80 with a synthetic manager, player character and parameters. The lookup of the
// player character (0x465590) is replaced by one that returns the synthetic character.
#include <cstdio>
#include <cstring>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t rensatsuUpdate = 0x43cc80;
constexpr uintptr_t playerCharacterLookup = 0x465590;
constexpr uintptr_t characterManagerGlobal = 0x9e9658;
constexpr uint32_t managerStateFunction = 0;
constexpr uint32_t managerInRensatsu = 0x22;
constexpr uint32_t characterParameters = 0xc;
constexpr uint32_t parameterGauge = 0x364;
constexpr uint32_t parameterGaugeMaximum = 0x368;
constexpr float gaugeMaximum = 1000.0f;
constexpr float drainPerUpdate = 2.3f;

alignas(4) uint8_t manager[0x100];
alignas(4) uint8_t character[0x100];
alignas(4) uint8_t parameters[0x400];
alignas(4) uint8_t characterManager[0x10];

float& gauge() { return *reinterpret_cast<float*>(parameters + parameterGauge); }

// A Spring Harvest state: thiscall with one argument, as the update calls [manager].
void __fastcall drainingState(void*, void*, int) { gauge() -= drainPerUpdate; }

// mov eax,<character>; ret
void returnSyntheticCharacter() {
    uint8_t* code = &codeByte(playerCharacterLookup);
    code[0] = 0xb8;
    const auto address = reinterpret_cast<uintptr_t>(character);
    std::memcpy(code + 1, &address, sizeof(address));
    code[5] = returnOpcode;
}

void runUpdate() {
    const uintptr_t function = va(rensatsuUpdate);
    uint8_t* managerAddress = manager;
    __asm {
        push ebx
        push esi
        push edi
        mov edi, managerAddress
        call function
        pop edi
        pop esi
        pop ebx
    }
}

void setUp(bool inRensatsu, float startingGauge) {
    std::memset(manager, 0, sizeof(manager));
    if (inRensatsu) {
        *reinterpret_cast<void**>(manager + managerStateFunction) = reinterpret_cast<void*>(&drainingState);
        manager[managerInRensatsu] = 1;
    }
    gauge() = startingGauge;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_rensatsu_gauge")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is claimed or hooked.
    codeByte(0x43ccc2) = 0x08;  // mov esi,[ecx+0xc] -> [ecx+0x8]: another offset of the parameters
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Full Spring Harvest Gauge: the Spring Harvest update is not the known version at 0x43cc80; the "
                     "tweak is skipped") &&
              loggedText("Tweak rensatsu_gauge could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(rensatsuUpdate) && codeByte(rensatsuUpdate) == 0x55 &&
              optionIndex(L"Full Spring Harvest Gauge") == noItem,
        "nothing is claimed, hooked or shown");
    codeByte(0x43ccc2) = 0x0c;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(rensatsuUpdate, 9) && codeByte(rensatsuUpdate) == jumpOpcode, "the update is hooked (9 bytes claimed)");
    const uint32_t item = optionIndex(L"Full Spring Harvest Gauge");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"つま斬りゲージMAX",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    returnSyntheticCharacter();
    *reinterpret_cast<uint8_t**>(character + characterParameters) = parameters;
    *reinterpret_cast<float*>(parameters + parameterGaugeMaximum) = gaugeMaximum;
    *reinterpret_cast<uintptr_t*>(va(characterManagerGlobal)) = reinterpret_cast<uintptr_t>(characterManager);

    setUp(false, 100.0f);
    runUpdate();
    check(gauge() == 100.0f && *reinterpret_cast<uint8_t**>(manager + 0x54) == character,
        "off: the update keeps the gauge and sets the player character");
    setUp(true, 500.0f);
    runUpdate();
    check(gauge() == 500.0f - drainPerUpdate, "off: a Spring Harvest drains the gauge");

    changeOption(item, 1);
    setUp(false, 100.0f);
    runUpdate();
    check(gauge() == gaugeMaximum, "on: the gauge is filled to its maximum");
    setUp(true, gaugeMaximum);
    for (int update = 0; update < 1000; ++update) runUpdate();
    check(gauge() == gaugeMaximum && manager[0x21] == 0,
        "on: through 1000 updates of a Spring Harvest the gauge stays full and the Spring Harvest goes on");
    *reinterpret_cast<float*>(parameters + parameterGaugeMaximum) = 1500.0f;
    runUpdate();
    check(gauge() == 1500.0f, "on: a changed maximum is followed");
    *reinterpret_cast<float*>(parameters + parameterGaugeMaximum) = gaugeMaximum;

    *reinterpret_cast<uintptr_t*>(va(characterManagerGlobal)) = 0;
    setUp(false, 100.0f);
    runUpdate();
    check(gauge() == 100.0f, "on: without a character manager (title screen) nothing is written");
    *reinterpret_cast<uintptr_t*>(va(characterManagerGlobal)) = reinterpret_cast<uintptr_t>(characterManager);

    changeOption(item, 0);
    setUp(false, 100.0f);
    runUpdate();
    check(gauge() == 100.0f, "off again: the gauge is left as it is");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"rensatsu_gauge", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [rensatsu_gauge] enabled=1");

    return finish("rensatsu_gauge_native_probe");
}
