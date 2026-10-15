// While challengers are in, each time the player enters the dojo 0x5ac1d0 calls 0x5acc80, which
// picks who they are anew. It takes the row of the dojo's reputation in ReputationData.l (the
// stage +0x1407e; 5 counts as 6 when every pupil is a woman, 0x5abe60), lets rand() % 100 choose
// one of the row's three groups of RaidNPCData.l by their weights, and writes the group's person
// definitions to +0x14174 and their number to +0x14082. The Dojo_yaburi script spawns them.
// This tweak writes a fixed group the same way, or lets the game pick at a fixed reputation.
#include "dojo_raid_lineup.h"
#include <cstdio>
#include "../game_code.h"

namespace tweaks {

namespace {

// RaidNPCData.l: an int32 count, then 0x10 bytes per group.
struct RaidGroup {
    uint16_t number;
    uint16_t personCount;
    int32_t personDefinitions[3];
};
static_assert(sizeof(RaidGroup) == 0x10, "RaidNPCData.l has 0x10 bytes per group");

// The dojo's tables that 0x5abf20 loads.
struct RaidTables {
    int32_t groupCount;            // The count at the start of RaidNPCData.l
    const uint8_t* raidGroupFile;  // RaidNPCData.l, starting with that count
    const uint8_t* reputationRows; // ReputationData.l
};
constexpr uint32_t groupFileHeaderSize = 4;

using ChooseChallengers = void(__stdcall*)(const RaidTables* tables, uint8_t* gameState);

constexpr uintptr_t chooseChallengers = 0x5acc80;
constexpr uint32_t chooseChallengersHookLength = 5;  // push ebp; mov ebp,esp; mov eax,[0x9e967c] (part)
constexpr uintptr_t chooseChallengersCaller = 0x5ac23b;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uint32_t stageOffset = 0x1407e;               // uint8: 0 to 6, the row of ReputationData.l
constexpr uint32_t challengerCountOffset = 0x14082;     // int16
constexpr uint32_t challengerDefinitionsOffset = 0x14174;  // int32[8]
constexpr uint32_t maxChallengers = 8;                  // 0x5acc80 writes at most 8
constexpr uint8_t allWomenStage = 6;                    // The game's other row for ☆5

constexpr uint32_t notFixed = 0;
constexpr uint32_t rankCount = 5;  // ☆1 to ☆5 (stage 1 to 5, and 6 as ☆5)
constexpr uint32_t noRank = 0;     // Stage 0: the game sends no challengers then

// The settings: fixedReputation is notFixed or the ☆ (1 to 5); fixedChallengers[☆ - 1] is
// notFixed or 1 + the index into that reputation's groups below.
uint32_t fixedReputation = notFixed;
uint32_t fixedChallengers[rankCount] = {};

// The groups of RaidNPCData.l each reputation picks from, in the order of its choices. ☆5 also
// lists the groups of the row for pupils who are all women (8, 10 and 15).
const uint16_t rank1Groups[] = {0, 1, 2};
const uint16_t rank2Groups[] = {3, 4, 5};
const uint16_t rank3Groups[] = {6, 7, 8};
const uint16_t rank4Groups[] = {10, 9, 11};
const uint16_t rank5Groups[] = {13, 12, 14, 15, 10, 8};
const uint16_t* const groupsOfRank[rankCount] = {rank1Groups, rank2Groups, rank3Groups, rank4Groups, rank5Groups};
const uint32_t groupCountOfRank[rankCount] = {_countof(rank1Groups), _countof(rank2Groups), _countof(rank3Groups),
    _countof(rank4Groups), _countof(rank5Groups)};

GameImage game;
TweakLog logger("Challenger Lineup");
ChooseChallengers originalChooseChallengers;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0xa1};  // push ebp; mov ebp,esp; mov eax,[0x9e967c]
    // push ebx; mov bl,[eax+0x1407e]; cmp bl,7
    const uint8_t readStage[] = {0x53, 0x8a, 0x98, 0x7e, 0x40, 0x01, 0x00, 0x80, 0xfb, 0x07};
    // mov edx,[ebp+8]; movzx eax,bl; lea ecx,[eax+eax*8]; mov eax,[edx+8]; lea ebx,[eax+ecx*4]
    const uint8_t findRow[] = {0x8b, 0x55, 0x08, 0x0f, 0xb6, 0xc3, 0x8d, 0x0c, 0xc0, 0x8b, 0x42, 0x08, 0x8d, 0x1c,
        0x88};
    // movzx eax,word [ebx+ecx*2+0x1e]; mov ecx,[ebp+8]; cwde; cmp eax,[ecx]; jge; mov edx,[ecx+4];
    // add eax,eax; lea ecx,[edx+eax*8+4]
    const uint8_t findGroup[] = {0x0f, 0xb7, 0x44, 0x4b, 0x1e, 0x8b, 0x4d, 0x08, 0x98, 0x3b, 0x01, 0x7d, 0x0b, 0x8b,
        0x51, 0x04, 0x03, 0xc0, 0x8d, 0x4c, 0xc2, 0x04};
    // mov esi,[ebp+0xc]; add esi,0x14174
    const uint8_t writeDefinitions[] = {0x8b, 0x75, 0x0c, 0x81, 0xc6, 0x74, 0x41, 0x01, 0x00};
    // mov ax,[ecx+2]; mov ecx,[ebp+0xc]; pop edi; mov [ecx+0x14082],ax
    const uint8_t writeCount[] = {0x66, 0x8b, 0x41, 0x02, 0x8b, 0x4d, 0x0c, 0x5f, 0x66, 0x89, 0x81, 0x82, 0x40, 0x01,
        0x00};
    const uint8_t pushArguments[] = {0x56, 0x53};  // push esi (the game state); push ebx (the tables)
    return game.codeIs(chooseChallengers, prologue, sizeof(prologue)) &&
           game.operandIs(chooseChallengers + 4, gameStateGlobal) &&
           game.codeIs(chooseChallengers + 8, readStage, sizeof(readStage)) &&
           game.codeIs(0x5acca8, findRow, sizeof(findRow)) &&
           game.codeIs(0x5accf8, findGroup, sizeof(findGroup)) &&
           game.codeIs(0x5acd1c, writeDefinitions, sizeof(writeDefinitions)) &&
           game.codeIs(0x5acd40, writeCount, sizeof(writeCount)) &&
           game.codeIs(chooseChallengersCaller - sizeof(pushArguments), pushArguments, sizeof(pushArguments)) &&
           game.callsTo(chooseChallengersCaller, chooseChallengers);
}

uint8_t* gameState() {
    return reinterpret_cast<uint8_t*>(game.readUint32(gameStateGlobal));
}

uint32_t rankOfStage(uint8_t stage) {
    if (stage == allWomenStage) return rankCount;
    return stage <= rankCount ? stage : noRank;
}

// Writes the group as 0x5acc80 does. False if RaidNPCData.l has no such group.
bool placeGroup(const RaidTables* tables, uint8_t* state, uint16_t groupIndex) {
    if (groupIndex >= tables->groupCount) return false;
    const auto* group = reinterpret_cast<const RaidGroup*>(
        tables->raidGroupFile + groupFileHeaderSize + groupIndex * sizeof(RaidGroup));
    auto* definitions = reinterpret_cast<int32_t*>(state + challengerDefinitionsOffset);
    for (uint32_t i = 0; i < group->personCount && i < maxChallengers; ++i)
        definitions[i] = group->personDefinitions[i];
    *reinterpret_cast<int16_t*>(state + challengerCountOffset) = static_cast<int16_t>(group->personCount);
    return true;
}

// 0x5acc80 picks from the row of the stage it reads from the game state, so the stage is the
// fixed one for that call only. ☆5 is written as 5: the game turns it into its row 6 itself
// when every pupil is a woman, as it would at that reputation.
void chooseAtRank(const RaidTables* tables, uint8_t* state, uint32_t rank) {
    uint8_t& stage = gameState()[stageOffset];
    const uint8_t gameStage = stage;
    stage = static_cast<uint8_t>(rank);
    originalChooseChallengers(tables, state);
    stage = gameStage;
}

// Runs once each time the player enters the dojo while challengers are in, so it may log.
void __stdcall chooseChallengersHook(const RaidTables* tables, uint8_t* state) {
    const uint8_t gameStage = gameState()[stageOffset];
    const uint32_t rank = fixedReputation != notFixed ? fixedReputation : rankOfStage(gameStage);
    if (rank == noRank) {
        originalChooseChallengers(tables, state);
        return;
    }
    char line[160];
    const uint32_t challengers = fixedChallengers[rank - 1];
    if (challengers != notFixed && challengers <= groupCountOfRank[rank - 1]) {
        const uint16_t groupIndex = groupsOfRank[rank - 1][challengers - 1];
        if (placeGroup(tables, state, groupIndex)) {
            sprintf_s(line, "challengers at the dojo set to group %u of RaidNPCData (option of reputation %u, "
                            "dojo stage %u)", groupIndex, rank, gameStage);
            logger.write("INFO", line);
            return;
        }
        sprintf_s(line, "RaidNPCData has no group %u (it has %d); the game picks the challengers", groupIndex,
            tables->groupCount);
        logger.write("WARN", line);
    }
    if (fixedReputation == notFixed) {
        originalChooseChallengers(tables, state);
        return;
    }
    chooseAtRank(tables, state, fixedReputation);
    sprintf_s(line, "challengers at the dojo picked at reputation %u (dojo stage %u)", fixedReputation, gameStage);
    logger.write("INFO", line);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the choice of challengers is not the known version", chooseChallengers);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, chooseChallengers, chooseChallengersHookLength,
            reinterpret_cast<void*>(&chooseChallengersHook), reinterpret_cast<void**>(&originalChooseChallengers))) {
        logger.installFailed(error, chooseChallengers);
        return false;
    }
    return true;
}

// 道場破り is "challenger" in the game's messages, and the owner rates the dojo as "Rep: ☆☆".
const Text dontFix{L"固定しない", L"Don't Fix"};
const Text reputationChoices[] = {dontFix, {L"☆1", L"☆1"}, {L"☆2", L"☆2"}, {L"☆3", L"☆3"}, {L"☆4", L"☆4"},
    {L"☆5", L"☆5"}};
const Text rank1Choices[] = {dontFix, {L"赤い着物の浪人", L"Red Ronin"}, {L"眼帯の武士", L"Samurai"},
    {L"町娘と兄貴", L"Lady and Thug"}};
const Text rank2Choices[] = {dontFix, {L"サングラスの大工", L"Sunglasses Carpenter"}, {L"外国人", L"Foreigner"},
    {L"渡世人と魔鱗組", L"Yakuza and Demonscale"}};
const Text rank3Choices[] = {dontFix, {L"赤い覆面浪人", L"Masked Red Ronin"}, {L"浪人傘浪人", L"Ronin-Kasa Ronin"},
    {L"猫耳忍者2人", L"Masked Bikini Women"}};
const Text rank4Choices[] = {dontFix, {L"ピンクの看護師", L"Foreign Nurse"}, {L"面のきれいな侍", L"Masked Clean Samurai"},
    {L"岡っ引きと同心", L"Thief-taker and Constable"}};
const Text rank5Choices[] = {dontFix, {L"天蓋の幽霊", L"Tengai Ronin"}, {L"二刀流の海兵隊", L"Royal Marine"},
    {L"老武士2人", L"Dual Old Samurai"}, {L"上裸の革命家", L"Half-naked Revolutionary"}, {L"ピンクの看護師", L"Foreign Nurse"},
    {L"猫耳忍者2人", L"Masked Bikini Women"}};
static_assert(_countof(rank1Choices) == _countof(rank1Groups) + 1 && _countof(rank2Choices) == _countof(rank2Groups) + 1 &&
                  _countof(rank3Choices) == _countof(rank3Groups) + 1 && _countof(rank4Choices) == _countof(rank4Groups) + 1 &&
                  _countof(rank5Choices) == _countof(rank5Groups) + 1,
    "each reputation has 固定しない and one choice per group");

const Option options[] = {
    choice(L"reputation", {L"道場破りの評判固定", L"Challenger Rep"}, reputationChoices, &fixedReputation, notFixed),
    choice(L"rank1", {L"☆1道場破り", L"☆1 Challenger"}, rank1Choices, &fixedChallengers[0], notFixed),
    choice(L"rank2", {L"☆2道場破り", L"☆2 Challenger"}, rank2Choices, &fixedChallengers[1], notFixed),
    choice(L"rank3", {L"☆3道場破り", L"☆3 Challenger"}, rank3Choices, &fixedChallengers[2], notFixed),
    choice(L"rank4", {L"☆4道場破り", L"☆4 Challenger"}, rank4Choices, &fixedChallengers[3], notFixed),
    choice(L"rank5", {L"☆5道場破り", L"☆5 Challenger"}, rank5Choices, &fixedChallengers[4], notFixed),
};

}  // namespace

const Tweak dojoRaidLineupTweak{L"dojo_raid_lineup", options, _countof(options), install};

}  // namespace tweaks
