// Installs mod.dll on a copy of the exe mapped into this process, registers a copy of the
// game's Script pack through the hooked 0x41dfd0, and checks the recruiting branches in the
// copy. The pack is read from the game installation and never written there.
#include <cstdio>
#include <vector>
#include "script_pack_probe.h"

using namespace mapped_exe;
using namespace script_pack_probe;

namespace {

constexpr int32_t templateMob = 1;
constexpr uint32_t talkedTo = 14;

bool targetsAre(const int16_t* targets, std::initializer_list<int16_t> expected) {
    size_t index = 0;
    for (int16_t value : expected)
        if (targets[index++] != value) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 4) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    const std::vector<uint8_t> scriptPackFile = readFile(argv[3]);
    if (scriptPackFile.size() < 0x1000) {
        std::printf("Could not read the action script pack %s; no test was run.\n", argv[3]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_recruit_success")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;
    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(registerScripts, 5), "the loading of the scripts is hooked once for the tweaks that change them");
    const uint32_t item = optionIndex(L"Guaranteed Recruiting");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"弟子勧誘確定成功",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    stubRegistration();

    std::vector<uint8_t> scriptPack = scriptPackFile;
    registerThroughLoader(scriptPackLoader, scriptPackSlot, scriptPack.data());
    check(scriptPack == scriptPackFile && targetsAre(targetsOf(scriptPack.data(), templateMob, talkedTo, 195),
                                              {197, 210, 210, 210, 210}),
        "off: the pack is unchanged (instruction 195 picks 1 in 5)");

    changeOption(item, 1);
    check(targetsAre(targetsOf(scriptPack.data(), templateMob, talkedTo, 195), {197, 197, 197, 197, 197}) &&
              targetsAre(targetsOf(scriptPack.data(), templateMob, talkedTo, 196), {197, 197}),
        "on: both rolls always go to the new pupil (197)");
    check(changedWordCount(scriptPack, scriptPackFile) == 5, "on: only those 5 targets are changed");

    changeOption(item, 0);
    check(scriptPack == scriptPackFile, "off again: the targets are written back");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"recruit_success", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [recruit_success] enabled=1");

    return finish("recruit_success_native_probe");
}
