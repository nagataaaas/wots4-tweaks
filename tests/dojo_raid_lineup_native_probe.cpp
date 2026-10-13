// Installs mod.dll on a copy of the exe mapped into this process and calls the hooked 0x5acc80
// with the game's RaidNPCData.l and ReputationData.l (copied into memory, read only), a synthetic
// game state and a stand-in rand in the copy's import table, for the settings of the options.
#include <cstdio>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t randImport = 0x9052f8;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uintptr_t definitionRootGlobal = 0x9e9640;  // 0x5abe60 only checks it is set without pupils
constexpr uintptr_t chooseChallengers = 0x5acc80;
constexpr uint32_t stageOffset = 0x1407e;
constexpr uint32_t pupilCountOffset = 0x1407d;
constexpr uint32_t challengerCountOffset = 0x14082;
constexpr uint32_t challengerDefinitionsOffset = 0x14174;
constexpr uint32_t gameStateSize = 0x48c00;
constexpr uint32_t reputationRowSize = 0x24;
constexpr uint32_t groupWeightsOffset = 0x18;  // int16[3] in a reputation row
constexpr uint32_t groupNumbersOffset = 0x1e;  // int16[3]

struct RaidTables {
    int32_t groupCount;
    const uint8_t* raidGroupFile;
    const uint8_t* reputationRows;
};

int standInRoll;
int __cdecl standInRand() { return standInRoll; }

std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

std::vector<uint8_t> raidGroupFile, reputationFile;
std::vector<uint8_t> gameState(gameStateSize);
RaidTables tables;

int16_t rowValue(uint32_t stage, uint32_t offset) {
    return *reinterpret_cast<const int16_t*>(reputationFile.data() + stage * reputationRowSize + offset);
}

// The group 0x5acc80 picks at the stage for a roll, read from ReputationData.l.
int groupAt(uint32_t stage, int roll) {
    int weightsBelow = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        weightsBelow += rowValue(stage, groupWeightsOffset + 2 * i);
        if (roll % 100 < weightsBelow) return rowValue(stage, groupNumbersOffset + 2 * i);
    }
    return -1;
}

int16_t challengerCount() { return *reinterpret_cast<int16_t*>(gameState.data() + challengerCountOffset); }
int32_t challenger(uint32_t index) {
    return reinterpret_cast<int32_t*>(gameState.data() + challengerDefinitionsOffset)[index];
}

// True if the game state holds the group of RaidNPCData.l, as 0x5acc80 writes it.
bool holdsGroup(int group) {
    const uint8_t* entry = raidGroupFile.data() + 4 + group * 0x10;
    const uint16_t count = *reinterpret_cast<const uint16_t*>(entry + 2);
    if (challengerCount() != count) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (challenger(i) != reinterpret_cast<const int32_t*>(entry + 4)[i]) return false;
    return true;
}

void enterDojo(uint8_t stage, int roll) {
    gameState[stageOffset] = stage;
    std::memset(gameState.data() + challengerDefinitionsOffset, 0xff, 8 * sizeof(int32_t));
    *reinterpret_cast<int16_t*>(gameState.data() + challengerCountOffset) = 0;
    standInRoll = roll;
    using Choose = void(__stdcall*)(const RaidTables*, uint8_t*);
    reinterpret_cast<Choose>(va(chooseChallengers))(&tables, gameState.data());
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 5) return 2;
    raidGroupFile = readFile(argv[3]);
    reputationFile = readFile(argv[4]);
    if (raidGroupFile.size() < 4 + 16 * 0x10 || reputationFile.size() < 7 * reputationRowSize) {
        std::printf("Could not read RaidNPCData.l and ReputationData.l (%s, %s); no test was run.\n", argv[3], argv[4]);
        return 3;
    }
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_dojo_raid_lineup")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    tables = {*reinterpret_cast<const int32_t*>(raidGroupFile.data()), raidGroupFile.data(), reputationFile.data()};
    static uint8_t definitionRoot[16];
    *reinterpret_cast<uintptr_t*>(va(randImport)) = reinterpret_cast<uintptr_t>(&standInRand);
    *reinterpret_cast<uintptr_t*>(va(gameStateGlobal)) = reinterpret_cast<uintptr_t>(gameState.data());
    *reinterpret_cast<uintptr_t*>(va(definitionRootGlobal)) = reinterpret_cast<uintptr_t>(definitionRoot);
    gameState[pupilCountOffset] = 0;  // 0x5abe60 is false then: ☆5 uses its usual row
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    codeByte(0x5accfc) = 0x1c;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Challenger Lineup: the choice of challengers is not the known version at 0x5acc80; the tweak "
                     "is skipped") &&
              loggedText("Tweak dojo_raid_lineup could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(chooseChallengers) && optionIndex(L"Challenger Rep") == noItem &&
              codeByte(chooseChallengers) == 0x55,
        "nothing is claimed, hooked or shown");
    codeByte(0x5accfc) = 0x1e;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(chooseChallengers, 5) && codeByte(chooseChallengers) == jumpOpcode,
        "0x5acc80 is hooked and claimed (5 bytes)");
    const uint32_t reputation = optionIndex(L"Challenger Rep");
    const wchar_t* rankLabels[] = {L"☆1 Challenger", L"☆2 Challenger", L"☆3 Challenger", L"☆4 Challenger",
        L"☆5 Challenger"};
    const uint32_t expectedChoices[] = {4, 4, 4, 4, 7};
    uint32_t rank[5];
    bool ranksShown = true;
    for (uint32_t i = 0; i < 5; ++i) {
        rank[i] = optionIndex(rankLabels[i]);
        ranksShown = ranksShown && rank[i] == reputation + 1 + i && optionItem(rank[i]).choiceCount == expectedChoices[i];
    }
    check(reputation != noItem && optionItem(reputation).choiceCount == 6 &&
              std::wstring(optionItem(reputation).label.japanese) == L"道場破りの評判固定" &&
              std::wstring(optionItem(reputation).choices[0].japanese) == L"固定しない" &&
              std::wstring(optionItem(reputation).choices[5].english) == L"☆5",
        "the reputation option has 固定しない and ☆1 to ☆5 in both languages");
    if (reputation == noItem) return 1;
    check(ranksShown && std::wstring(optionItem(rank[3]).choices[1].japanese) == L"ピンクの看護師" &&
              std::wstring(optionItem(rank[4]).choices[4].english) == L"Half-naked Revolutionary",
        "one option per reputation follows it, with 4 choices for ☆1 to ☆4 and 7 for ☆5");
    if (!ranksShown) return 1;
    check(optionValue(reputation) == 0 && optionValue(rank[0]) == 0 && optionValue(rank[4]) == 0,
        "without config.ini nothing is fixed");

    enterDojo(2, 50);
    check(holdsGroup(groupAt(2, 50)) && groupAt(2, 50) == 4, "default: the game picks from its row (stage 2, roll 50: group 4)");

    changeOption(reputation, 3);
    enterDojo(1, 50);
    check(holdsGroup(groupAt(3, 50)) && gameState[stageOffset] == 1,
        "☆3 fixed: the game picks from row 3 at stage 1, and the stage stays 1");
    changeOption(reputation, 5);
    enterDojo(2, 99);
    check(holdsGroup(groupAt(5, 99)) && groupAt(5, 99) == 14 && gameState[stageOffset] == 2,
        "☆5 fixed: row 5 without pupils who are all women (roll 99: group 14)");

    changeOption(reputation, 3);
    changeOption(rank[2], 3);
    enterDojo(1, 0);
    check(holdsGroup(8) && challengerCount() == 2 && challenger(0) == 0x13b && challenger(1) == 0x13c,
        "☆3 and 猫耳忍者2人 fixed: group 8 (definitions 0x13b and 0x13c)");

    changeOption(reputation, 0);
    changeOption(rank[3], 1);
    enterDojo(4, 0);
    check(holdsGroup(10) && challengerCount() == 1 && challenger(0) == 0x13f,
        "reputation not fixed, ☆4 ピンクの看護師 fixed: at stage 4, group 10 (0x13f)");
    enterDojo(2, 0);
    check(holdsGroup(groupAt(2, 0)), "the ☆4 option does not change the challengers at stage 2");
    changeOption(rank[4], 4);
    enterDojo(6, 0);
    check(holdsGroup(15) && challenger(0) == 0x23d, "stage 6 counts as ☆5: 上裸の革命家 is group 15 (0x23d)");
    changeOption(rank[4], 1);
    enterDojo(5, 0);
    check(holdsGroup(13) && challenger(0) == 0x141, "☆5 天蓋の幽霊 is group 13 (0x141, model UL_Ronin01)");
    changeOption(rank[4], 4);
    enterDojo(0, 0);
    check(holdsGroup(groupAt(0, 0)), "stage 0 has no reputation: the game picks");
    changeOption(rank[3], 0);
    enterDojo(4, 70);
    check(holdsGroup(groupAt(4, 70)), "固定しない again: the game picks at stage 4");
    check(loggedText("[INFO] Challenger Lineup: challengers at the dojo set to group 15 of RaidNPCData (option of "
                     "reputation 5, dojo stage 6)."),
        "a fixed group is logged with its group, reputation and stage");

    changeOption(reputation, 2);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"dojo_raid_lineup", L"reputation", 99, configPath().c_str()) == 2 &&
              GetPrivateProfileIntW(L"dojo_raid_lineup", L"rank3", 99, configPath().c_str()) == 3 &&
              GetPrivateProfileIntW(L"dojo_raid_lineup", L"rank5", 99, configPath().c_str()) == 4,
        "the options save writes [dojo_raid_lineup] reputation=2, rank3=3 and rank5=4");

    return finish("dojo_raid_lineup_native_probe");
}
