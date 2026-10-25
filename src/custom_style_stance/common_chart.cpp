#include "common_chart.h"
#include <cstring>
#include "game_calls.h"

namespace tweaks::custom_style_stance {

namespace {

// One row per defence move: the chart's move numbers are below 128 like a stance's.
constexpr int rowLimit = 128;
constexpr int defenceLimit = 128;  // Defence move indices (SkillData +8) stay below it (0x59da20)
// Renki Koho fills the table's field of one stance (0x59b730, indices 44-76), and only the
// field of the stance fought in is read (0x485820): only the slot's own stance's one works.
constexpr int firstStanceOnlyDefence = 44;
constexpr int lastStanceOnlyDefence = 76;
// The Kata (0x59b730, indices 83-88) are left out: chosen from here, they had no effect in the
// game (user check 2026-10-26).
constexpr int firstKataDefence = 83;
constexpr int lastKataDefence = 88;

// The learned record: 1 byte per stance and move number, at [0x9e967c] + 0x18394 (0x657f44).
// 3 is learned but not yet for My Style, as 0 (0x657f51).
constexpr uint32_t learnedRecords = 0x18394;
constexpr uint32_t recordsPerStance = 0x80;
constexpr uint8_t notYetForMyStyle = 3;
const uintptr_t* gameParameters;  // Relocated 0x9e967c

// The tweak's SkillComboData for a slot: the head (36 bytes), its tree and its moves. The tree
// is a chain of nodes up the higher side, keys rising; the game finds a key in it as in a
// stance's tree (0x657e30). The first moves' list holds every move, in the rows' order, as
// 0x655a50 makes a top card for each.
constexpr uint32_t comboFirstMoves = 0x18;  // int16 vector: begin, end, capacity
constexpr uint32_t comboCount = 0x10;
constexpr uint32_t comboRoot = 0x4;
struct ComboNode {
    uint8_t unused[8];
    ComboNode* lower;
    ComboNode* higher;
    int16_t key;
    uint8_t unused12[2];
    uint8_t* entry;
};
static_assert(sizeof(ComboNode) == 0x18 && offsetof(ComboNode, entry) == 0x14, "the game's tree node");

constexpr int chartMethodCount = 5;  // SkillChartChoice's vtable (0x99b55c): 5 methods
constexpr int fillMethod = 1;        // +4: fills the chart from a combo (0x657440)

struct CommonData {
    void* chart = nullptr;  // The chart made from this data, or null
    uint8_t combo[36] = {};
    ComboNode nodes[rowLimit] = {};
    int16_t firstMoves[rowLimit] = {};
    // The vtable's slot before the methods (the RTTI), then the methods.
    void* methods[1 + chartMethodCount] = {};
};
CommonData slotData[slotCount];
// The moves of all slots in one block, so a card's move is known as one of them by its address.
uint8_t entries[slotCount][rowLimit][comboEntrySize];
uint8_t learned[slotCount][rowLimit];  // 1 when the row's move was learned in any stance

using FillChart = void(__thiscall*)(void* chart, void* combo, int* stance);
FillChart gameFillChart;

uint8_t learnedRecord(int stance, int number) {
    const uintptr_t parameters = *gameParameters;
    if (!parameters || number < 0 || number >= static_cast<int>(recordsPerStance)) return 0;
    const uint8_t record =
        *reinterpret_cast<const uint8_t*>(parameters + learnedRecords + recordsPerStance * stance + number);
    return record == notYetForMyStyle ? 0 : record;
}

bool worksOnlyInOwnStance(int defence) {
    return defence >= firstStanceOnlyDefence && defence <= lastStanceOnlyDefence;
}

bool isKata(int defence) {
    return defence >= firstKataDefence && defence <= lastKataDefence;
}

// For each defence move, the stance's move it is copied from: the slot's stance's when it has
// one (its number is used again when the style is made), else a stance where it was learned.
struct Row {
    void* entry = nullptr;
    bool fromSlotStance = false;
    bool learned = false;
};

void findRows(int slotStance, Row (&rows)[defenceLimit]) {
    for (int stance = 0; stance < stanceCount; ++stance) {
        void* combo = comboOf(stance);
        if (!combo) continue;
        const int count = field<int>(combo, comboCount);
        for (int index = 0; index < count; ++index) {
            void* entry = comboMoveAt(combo, index);
            if (!entry) break;
            const int defence = defenceIndex(field<int16_t>(entry, ComboEntry::skill));
            if (defence < 0 || defence >= defenceLimit || isKata(defence)) continue;
            if (worksOnlyInOwnStance(defence) && stance != slotStance) continue;
            Row& row = rows[defence];
            const bool learnedHere = learnedRecord(stance, field<int16_t>(entry, ComboEntry::number)) != 0;
            const bool better = !row.entry || (stance == slotStance) || (!row.fromSlotStance && !row.learned && learnedHere);
            if (better) {
                row.entry = entry;
                row.fromSlotStance = stance == slotStance;
            }
            row.learned = row.learned || learnedHere;
        }
    }
}

// A copy of the move as a top card of its own: no links, drawn in the first column.
void copyMove(uint8_t* copy, void* entry, int row) {
    std::memcpy(copy, entry, comboEntrySize);
    field<int16_t>(copy, ComboEntry::number) = static_cast<int16_t>(row);
    field<uint8_t>(copy, ComboEntry::linkCount) = 0;
    field<int16_t*>(copy, ComboEntry::links) = nullptr;
    field<uint8_t>(copy, ComboEntry::otherCount) = 0;
    field<int16_t*>(copy, ComboEntry::others) = nullptr;
    field<int8_t>(copy, ComboEntry::column) = 0;
    field<int8_t>(copy, ComboEntry::row) = static_cast<int8_t>(row);
}

int makeCombo(int slot, int slotStance) {
    Row rows[defenceLimit];
    findRows(slotStance, rows);
    CommonData& data = slotData[slot];
    int count = 0;
    for (int defence = 0; defence < defenceLimit && count < rowLimit; ++defence) {
        if (!rows[defence].entry) continue;
        uint8_t* entry = entries[slot][count];
        copyMove(entry, rows[defence].entry, count);
        learned[slot][count] = rows[defence].learned ? 1 : 0;
        ComboNode& node = data.nodes[count];
        node = ComboNode();
        node.key = static_cast<int16_t>(count);
        node.entry = entry;
        if (count > 0) data.nodes[count - 1].higher = &node;
        data.firstMoves[count] = static_cast<int16_t>(count);
        ++count;
    }
    std::memset(data.combo, 0, sizeof(data.combo));
    if (count == 0) return 0;
    field<ComboNode*>(data.combo, comboRoot) = &data.nodes[0];
    field<int>(data.combo, comboCount) = count;
    field<int16_t*>(data.combo, comboFirstMoves) = data.firstMoves;
    field<int16_t*>(data.combo, comboFirstMoves + 4) = data.firstMoves + count;
    field<int16_t*>(data.combo, comboFirstMoves + 8) = data.firstMoves + count;
    return count;
}

// The common chart's vtable +4: the game fills a chart from the edited slot's stance's combo
// (0x5cbef0); the common chart is filled from its own one, the stance kept for the game's checks.
void __fastcall fillCommonChart(void* chart, void*, void* stanceCombo, int* stance) {
    for (CommonData& data : slotData) {
        if (data.chart == chart) {
            gameFillChart(chart, data.combo, stance);
            return;
        }
    }
    gameFillChart(chart, stanceCombo, stance);
}

// --- The learned record of a card -------------------------------------------------------------

// The common chart's moves are not in any stance's record under their numbers: the card reads
// whether the row's move was learned in any stance instead.
const uint8_t* __cdecl learnedOfCommonMove(const uint8_t* entry) {
    const uint8_t* first = &entries[0][0][0];
    if (entry < first || entry >= first + sizeof(entries)) return nullptr;
    const auto index = static_cast<size_t>(entry - first) / comboEntrySize;
    return &learned[0][0] + index;
}

uintptr_t afterRecordRead;  // Relocated 0x657f4f: mov bl,[eax], the record read

// Called from 0x657f2b in place of mov eax,[edi] (the stance); movsx edx,word ptr [esi] (the
// move's number). esi = the move, ecx = the game's parameters, both kept. For a common move it
// goes on at the read with eax = its learned byte.
__declspec(naked) void readRecordOfMove() {
    __asm {
        push ecx
        push edx
        push esi
        call learnedOfCommonMove
        add esp, 4
        pop edx
        pop ecx
        movsx edx, word ptr [esi]
        test eax, eax
        jz gameRecord
        push eax
        mov eax, afterRecordRead
        mov dword ptr [esp + 4], eax  // Return to the read instead
        pop eax
        ret
    gameRecord:
        mov eax, dword ptr [edi]
        ret
    }
}

CodePatch patches[] = {
    // mov eax,[edi]; movsx edx,word ptr [esi] -> call readRecordOfMove (set at install)
    CodePatch(0x657f2b, {0xe8, 0, 0, 0, 0}),
};

const wchar_t* japaneseName = L"共通";
const wchar_t* englishName = L"Common";

}  // namespace

bool installCommonChart(const wots4::ModContext& context, const GameImage& game, const TweakLog& log) {
    const uint8_t recordRead[] = {0x8b, 0x07, 0x0f, 0xbf, 0x16, 0x83, 0xf8, 0xff};  // ...; cmp eax,-1
    const uint8_t recordAt[] = {0x8d, 0x84, 0x08, 0x94, 0x83, 0x01, 0x00};          // 0x657f44
    const uint8_t recordUse[] = {0x8a, 0x18, 0x80, 0xfb, 0x03};                     // 0x657f4f mov bl,[eax]; cmp bl,3
    const uint8_t parameters[] = {0x8b, 0x0d};                                     // 0x657f1f mov ecx,[0x9e967c]
    if (!game.codeIs(0x657f2b, recordRead, sizeof(recordRead)) || !game.codeIs(0x657f44, recordAt, sizeof(recordAt)) ||
        !game.codeIs(0x657f4f, recordUse, sizeof(recordUse)) || !game.codeIs(0x657f1f, parameters, sizeof(parameters)) ||
        !game.operandIs(0x657f21, 0x9e967c)) {
        log.installFailed("the chart's learned record code is not the known version", 0x657f2b);
        return false;
    }
    gameParameters = reinterpret_cast<const uintptr_t*>(game.address(0x9e967c));
    afterRecordRead = game.address(0x657f4f);
    constexpr uint32_t callLength = 5;
    const uintptr_t from = game.address(patches[0].preferredAddress());
    const auto offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&readRecordOfMove) - (from + callLength));
    uint8_t call[callLength] = {0xe8};
    std::memcpy(call + 1, &offset, sizeof(offset));
    patches[0].setPatchedBytes(call);
    return claimPatches(context, game, patches, _countof(patches), log);
}

void setCommonChartEnabled(bool enabled, const TweakLog& log) {
    writePatches(patches, _countof(patches), enabled, log);
}

bool prepareCommonChart(void* chart, int slot, int slotStance) {
    if (!chart || slot < 0 || slot >= slotCount) return false;
    CommonData& data = slotData[slot];
    data.chart = nullptr;
    if (makeCombo(slot, slotStance) == 0) return false;
    void** gameMethods = *reinterpret_cast<void***>(chart);
    std::memcpy(data.methods, gameMethods - 1, sizeof(data.methods));
    gameFillChart = reinterpret_cast<FillChart>(gameMethods[fillMethod]);
    data.methods[1 + fillMethod] = reinterpret_cast<void*>(&fillCommonChart);
    *reinterpret_cast<void***>(chart) = data.methods + 1;
    data.chart = chart;
    return true;
}

void forgetCommonChart(int slot) {
    if (slot < 0 || slot >= slotCount) return;
    slotData[slot].chart = nullptr;
}

const wchar_t* commonChartName() {
    constexpr wchar_t firstJapaneseCharacter = 0x3000;
    const wchar_t* stance = stanceName(0);
    return stance && stance[0] >= firstJapaneseCharacter ? japaneseName : englishName;
}

}  // namespace tweaks::custom_style_stance
