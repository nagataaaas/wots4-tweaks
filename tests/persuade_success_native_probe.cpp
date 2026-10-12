// Installs mod.dll on a copy of the exe mapped into this process and registers copies of the
// game's action script packs through the hooked 0x41dfd0, then checks the wooing branches in
// the copies. The packs are read from the game installation and never written there.
#include <cstdio>
#include <cstring>
#include <vector>
#include "script_pack_probe.h"

using namespace mapped_exe;
using namespace script_pack_probe;

namespace {

constexpr int32_t templateMob = 1, akagi = 10, kogure = 11, moro = 14, dojima = 65;
constexpr uint32_t talkedTo = 14, attacked = 5;
constexpr size_t wooingWordCount = 1 + 188;  // TemplateMob 406, and the 7 named characters

bool allTargetsAre(const int16_t* targets, uint32_t count, int16_t value) {
    for (uint32_t index = 0; index < count; ++index)
        if (targets[index] != value) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    const std::vector<uint8_t> scriptPackFile = readFile(argv[3]);
    const std::vector<uint8_t> userCommandPackFile = readFile(argv[4]);
    if (scriptPackFile.size() < 0x1000 || userCommandPackFile.size() < 0x1000) {
        std::printf("Could not read the action script packs %s and %s; no test was run.\n", argv[3], argv[4]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_persuade_success")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    codeByte(0x41e54a) = 0xb6;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Guaranteed Wooing: the loading of the action scripts is not the known version at 0x41dfd0; "
                     "the tweak is skipped") &&
              loggedText("Tweak persuade_success could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(registerScripts) && optionIndex(L"Guaranteed Wooing") == noItem && codeByte(registerScripts) == 0x55,
        "nothing is claimed, hooked or shown");
    codeByte(0x41e54a) = 0xb7;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(registerScripts, 5) && codeByte(registerScripts) == jumpOpcode, "0x41dfd0 is claimed (5 bytes) and hooked");
    const uint32_t item = optionIndex(L"Guaranteed Wooing");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"口説き確定成功",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    stubRegistration();

    std::vector<uint8_t> userCommandPack = userCommandPackFile;
    registerThroughLoader(userCommandPackLoader, userCommandPackSlot, userCommandPack.data());
    check(userCommandPack == userCommandPackFile && !loggedText("Guaranteed Wooing: action script"),
        "the UserCommand pack is left alone, with nothing logged");

    std::vector<uint8_t> scriptPack = scriptPackFile;
    registerThroughLoader(scriptPackLoader, scriptPackSlot, scriptPack.data());
    check(scriptPack == scriptPackFile && !loggedText("Guaranteed Wooing: action script"),
        "off: the Script pack is registered unchanged, and every script matches");

    changeOption(item, 1);
    check(changedWordCount(scriptPack, scriptPackFile) == wooingWordCount, "on: 189 jump targets are changed");
    check(targetsOf(scriptPack.data(), templateMob, talkedTo, 406)[0] == 407,
        "on: TemplateMob 406 goes on to the promise (407) instead of the refusal (423)");
    check(allTargetsAre(targetsOf(scriptPack.data(), akagi, attacked, 45), 8, 51) &&
              allTargetsAre(targetsOf(scriptPack.data(), akagi, attacked, 50), 2, 51) &&
              allTargetsAre(targetsOf(scriptPack.data(), akagi, attacked, 51), 2, 77),
        "on: Akagi's first rolls all go to the second roll, which goes to the promise (77)");
    check(allTargetsAre(targetsOf(scriptPack.data(), kogure, attacked, 53), 5, 79),
        "on: Kogure's 1-in-5 second roll always goes to the promise (79)");
    check(allTargetsAre(targetsOf(scriptPack.data(), dojima, attacked, 18), 8, 24) &&
              allTargetsAre(targetsOf(scriptPack.data(), dojima, attacked, 25), 5, 53),
        "on: Dojima's rolls go to 24 and then to the promise (53)");

    changeOption(item, 0);
    check(scriptPack == scriptPackFile, "off again: every target is written back");

    // A script of another version is left alone; the others are still changed.
    std::vector<uint8_t> otherVersion = scriptPackFile;
    uint8_t* moroScript = tweaks::script_patch::findScript(otherVersion.data(), moro);
    *reinterpret_cast<uint16_t*>(moroScript + 8 + attacked * 8 + 4) += 1;
    const std::vector<uint8_t> otherVersionFile = otherVersion;
    changeOption(item, 1);
    registerThroughLoader(scriptPackLoader, scriptPackSlot, otherVersion.data());
    check(loggedText("[WARN] Guaranteed Wooing: action script 14 is not the known version; it is left unchanged."),
        "a script that does not match is logged");
    check(changedWordCount(otherVersion, otherVersionFile) == wooingWordCount - 26 &&
              targetsOf(otherVersion.data(), moro, attacked, 63)[0] == 64,
        "only the other scripts are changed (Moro's 26 targets are kept)");

    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"persuade_success", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [persuade_success] enabled=1");

    return finish("persuade_success_native_probe");
}
