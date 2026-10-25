#include "stance_rules.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include "game_calls.h"
#include "my_style_screen.h"
#include "stance_charts.h"

namespace tweaks::custom_style_stance {

namespace {

constexpr uint32_t uiTexts = 5;
constexpr uint32_t firstMoveTakenText = 706;  // 初段の技のコマンドが別の技で設定されています
constexpr uint32_t defenceTakenText = 744;    // 同じ種類の防御技は1つしか設定できません
constexpr uint32_t noClash = 0;
constexpr int notDefence = -1;

const wots4::HostApi* host = nullptr;
const TweakLog* logger = nullptr;

// What decides whether two chosen cards may be in one style, as the game decides it for the
// cards of one chart (0x6587da, 0x658623): the text of the message that refuses them, or 0.
// The game compares first move commands of attacks only (0x6585a0): defence moves are first
// moves with command 0, and differ by their type instead.
uint32_t clashBetween(void* card, void* other) {
    const int skill = skillOf(card);
    const int otherSkill = skillOf(other);
    const bool defence = defenceIndex(skill) != notDefence;
    if (defence != (defenceIndex(otherSkill) != notDefence)) return noClash;
    if (defence) return defenceType(skill) == defenceType(otherSkill) ? defenceTakenText : noClash;
    if (isFirstMove(card) && isFirstMove(other) &&
        field<uint16_t>(card, Card::command) == field<uint16_t>(other, Card::command)) {
        return firstMoveTakenText;
    }
    return noClash;
}

// The message that refuses a card, and the stance of the chosen card it clashes with.
struct Clash {
    uint32_t text = noClash;
    int stance = noStance;

    explicit operator bool() const { return text != noClash; }
};

Clash clashWithCharts(void* card, const StanceChart* charts, int count) {
    Clash clash;
    for (int index = 0; index < count && !clash; ++index) {
        forEachCard(charts[index].chart, [&](void* other) {
            if (!clash && isChosen(other)) clash.text = clashBetween(card, other);
        });
        if (clash) clash.stance = charts[index].stance;
    }
    return clash;
}

// --- Ability points ---------------------------------------------------------------------------

// The game adds up the chosen cards of the charts in SharedData::slotCharts (0x5c9f80); these
// are the points of the slots' other charts.
int __cdecl pointsOfOtherCharts(void* shared) {
    int points = 0;
    for (int slot = 0; slot < slotCount; ++slot) {
        StanceChart charts[stanceCount];
        const int count = chartsOfSlot(shared, slot, charts);
        void* counted = field<void*>(shared, SharedData::slotCharts + slot * sizeof(void*));
        for (int index = 0; index < count; ++index) {
            if (charts[index].chart == counted) continue;
            forEachCard(charts[index].chart, [&](void* card) {
                if (isChosen(card)) points += field<int16_t>(card, Card::cost);
            });
        }
    }
    return points;
}

// Called from 0x5c9ff7 in place of mov eax,[ebp-0x4c] (the total); movsx ecx,ax. esi = shared.
__declspec(naked) void addPointsOfOtherCharts() {
    __asm {
        push esi
        call pointsOfOtherCharts
        add esp, 4
        add eax, dword ptr [ebp - 0x4c]
        movsx ecx, ax
        ret
    }
}

// --- Choosing a card --------------------------------------------------------------------------

constexpr uint32_t messageLength = 0x40;   // wchar_t, as 0x5cb824 copies a message
constexpr uint32_t screenMessage = 0x3bc;  // The message the move select shows (0x5cb6fd)

constexpr wchar_t firstJapaneseCharacter = 0x3000;

// The refusing text followed by the stance whose chart takes the card's command or type:
// "...（上段）" or "... (Jo-dan)". The stance is left out when it would not fit.
void writeClashMessage(wchar_t* message, const Clash& clash) {
    const wchar_t* text = findUiText(host, clash.text);
    if (!text) return;
    wcsncpy_s(message, messageLength, text, _TRUNCATE);
    const wchar_t* name = stanceName(clash.stance);
    if (!name || name[0] == 0) return;
    wchar_t stance[messageLength];
    const bool japanese = name[0] >= firstJapaneseCharacter;
    if (swprintf_s(stance, japanese ? L"（%s）" : L" (%s)", name) < 0) return;
    if (wcslen(message) + wcslen(stance) < messageLength) wcscat_s(message, messageLength, stance);
}

// A top card is free unless a chosen card of its chart takes its first move command; the cards
// below it stay disabled until the card before them is chosen. Only first moves are dimmed: the
// game explains a dimmed first move with 706 or 744 (0x5cb7a2), but other dimmed cards with
// "the move before is not chosen" (705).
bool canBeTaken(void* card) {
    const int state = field<int>(card, Card::state);
    if (state != cardFree && state != cardDisabled) return false;
    return isFirstMove(card) && isTopCard(card);
}

// Before the move select chooses the card under the cursor, refuses it as the game refuses a
// card whose command is taken in the same chart, when a chosen card of the slot's other charts
// takes it, and names that card's stance. For a free card, a message already written (not
// enough points) is left as it is; a card dimmed for another chart gets the stance added.
void __cdecl refuseClashingCard(wchar_t* message) {
    void* screen = reinterpret_cast<uint8_t*>(message) - screenMessage;
    void* chart = field<void*>(screen, MoveSelectScreen::shownChart);
    void* card = chart ? field<void*>(chart, Chart::cursorCard) : nullptr;
    if (!card) return;
    const int state = field<int>(card, Card::state);
    if (state == cardFree && message[0] != 0) return;
    if (state != cardFree && !(state == cardDisabled && canBeTaken(card))) return;
    StanceChart others[stanceCount];
    int slotStance;
    const int count = chartsBesides(chart, others, slotStance);
    if (count == 0) return;
    const StanceChart own[] = {{chart, slotStance}};
    if (state == cardDisabled && clashWithCharts(card, own, 1)) return;  // The game's message stands
    const Clash clash = clashWithCharts(card, others, count);
    if (clash) writeClashMessage(message, clash);
}

// Called from 0x5cb832 in place of mov edi,[esp+0xc] (the message); xor ecx,ecx.
__declspec(naked) void checkCardBeforeChoosing() {
    __asm {
        pushad
        push dword ptr [esp + 0x20 + 4 + 0xc]  // Past the registers and the return address
        call refuseClashingCard
        add esp, 4
        popad
        mov edi, dword ptr [esp + 4 + 0xc]
        xor ecx, ecx
        ret
    }
}

// --- Dimming taken cards ----------------------------------------------------------------------

void dimTakenCards(void* chart) {
    if (!chart) return;
    StanceChart others[stanceCount];
    int slotStance;
    const int count = chartsBesides(chart, others, slotStance);
    if (count == 0) return;
    const StanceChart own[] = {{chart, slotStance}};
    forEachCard(chart, [&](void* card) {
        if (!canBeTaken(card)) return;
        const bool taken = clashWithCharts(card, own, 1) || clashWithCharts(card, others, count);
        const int state = taken ? cardDisabled : cardFree;
        if (field<int>(card, Card::state) != state) setCardState(card, state);
    });
}

using LetGoCard = void(__stdcall*)(void* chart, void* card);
using ResetChart = void(__stdcall*)(void* chart);
LetGoCard originalLetGoCard;
ResetChart originalResetChart;

// 0x658900: the chart brightens the cards the let-go card took, also those taken on other charts.
void __stdcall letGoCard(void* chart, void* card) {
    originalLetGoCard(chart, card);
    dimTakenCards(chart);
}

// 0x657b10: Auto-Select and Copy Stance start from a reset chart and skip disabled cards, so
// dimming here keeps them off the commands taken on other charts.
void __stdcall resetChart(void* chart) {
    originalResetChart(chart);
    dimTakenCards(chart);
}

// --- Confirming the style ---------------------------------------------------------------------

using ConfirmCheck = bool(__stdcall*)(void* topScreen);
ConfirmCheck originalEverySlotHasAttack;
ConfirmCheck originalRefuseStyle;

void* sharedOfTop(void* topScreen) {
    return field<void*>(topScreen, TopScreen::shared);
}

bool slotHasAttack(void* shared, int slot) {
    StanceChart charts[stanceCount];
    const int count = chartsOfSlot(shared, slot, charts);
    if (count == 0) return true;  // As 0x5d3c37: a slot without a chart is not checked
    bool attack = false;
    for (int index = 0; index < count && !attack; ++index) {
        forEachCard(charts[index].chart, [&](void* card) {
            if (!attack && isChosen(card) && defenceIndex(skillOf(card)) == notDefence) attack = true;
        });
    }
    return attack;
}

// 0x5d3c00: every stance of the style has a chosen attack move, on any of its charts.
bool __stdcall everySlotHasAttack(void* topScreen) {
    if (originalEverySlotHasAttack(topScreen)) return true;
    void* shared = sharedOfTop(topScreen);
    for (int slot = 0; slot < slotCount; ++slot) {
        if (!slotHasAttack(shared, slot)) return false;
    }
    return true;
}

// Auto-Select and Copy Stance choose on the slot's own chart only, so they can take a command
// chosen on another chart. The style is refused as the move select would refuse the card.
uint32_t clashInStyle(void* shared) {
    for (int slot = 0; slot < slotCount; ++slot) {
        StanceChart charts[stanceCount];
        const int count = chartsOfSlot(shared, slot, charts);
        for (int index = 0; index + 1 < count; ++index) {
            uint32_t clash = noClash;
            forEachCard(charts[index].chart, [&](void* card) {
                if (clash == noClash && isChosen(card)) {
                    clash = clashWithCharts(card, charts + index + 1, count - index - 1).text;
                }
            });
            if (clash != noClash) return clash;
        }
    }
    return noClash;
}

// 0x5d3ab0: true when it showed why the style cannot be created.
bool __stdcall refuseStyle(void* topScreen) {
    if (originalRefuseStyle(topScreen)) return true;
    const uint32_t clash = clashInStyle(sharedOfTop(topScreen));
    if (clash == noClash) return false;
    void* window = openMessageWindow({uiTexts, clash});
    if (!window) return false;
    field<void*>(topScreen, TopScreen::messageWindow) = window;
    return true;
}

// --- Creating the style -----------------------------------------------------------------------

constexpr int numberCount = 128;  // Move numbers are below 128 (asserts at 0x59a00c and others)
constexpr int noNumber = -1;
constexpr int maxLinks = 16;      // A combo move leads to at most 4 (see custom_style_limit.md)

void* originalAddChosenMoves;

struct NumberPlan {
    bool taken[numberCount] = {};
    int next = 0;
    int skipped = 0;

    int take() {
        while (next < numberCount && taken[next]) ++next;
        if (next == numberCount) return noNumber;
        taken[next] = true;
        return next;
    }
};

// Returns how many moves were added.
int addMovesOfChart(void* table, void* chart, NumberPlan& numbers) {
    int added = 0;
    // The chart's chosen moves get numbers that the slot's stance does not use; links between
    // them follow, links to other stances' moves do not exist.
    int16_t renumbered[numberCount];
    for (int16_t& number : renumbered) number = noNumber;
    forEachCard(chart, [&](void* card) {
        void* entry = field<void*>(card, Card::entry);
        if (!isChosen(card) || !entry) return;
        const int number = field<int16_t>(entry, ComboEntry::number);
        if (number < 0 || number >= numberCount || renumbered[number] != noNumber) return;
        const int free = numbers.take();
        if (free == noNumber) {
            ++numbers.skipped;
            return;
        }
        renumbered[number] = static_cast<int16_t>(free);
    });
    forEachCard(chart, [&](void* card) {
        void* entry = field<void*>(card, Card::entry);
        if (!isChosen(card) || !entry) return;
        const int number = field<int16_t>(entry, ComboEntry::number);
        if (number < 0 || number >= numberCount || renumbered[number] == noNumber) return;
        int16_t links[maxLinks];
        int8_t linkCount = 0;
        const int16_t* targets = field<const int16_t*>(entry, ComboEntry::links);
        const int targetCount = field<uint8_t>(entry, ComboEntry::linkCount);
        for (int index = 0; index < targetCount && linkCount < maxLinks; ++index) {
            const int target = targets[index];
            if (target >= 0 && target < numberCount && renumbered[target] != noNumber) {
                links[linkCount++] = renumbered[target];
            }
        }
        if (addMove(table, renumbered[number], field<int16_t>(entry, ComboEntry::skill),
                field<uint8_t>(entry, ComboEntry::mark), links, linkCount)) {
            ++added;
        }
    });
    return added;
}

// After the game put the chosen moves of the slot's chart into the slot's table (0x5d3660,
// before the table's stance is written at 0x5d34b5), puts in those of the slot's other charts.
bool __cdecl addMovesOfOtherCharts(void* table, void* chart) {
    StanceChart others[stanceCount];
    int slotStance;
    const int count = chartsBesides(chart, others, slotStance);
    if (count == 0) return false;
    void* combo = comboOf(slotStance);
    if (!combo) return false;
    NumberPlan numbers;
    for (int number = 0; number < numberCount; ++number) numbers.taken[number] = comboHasNumber(combo, number);
    int added = 0;
    for (int index = 0; index < count; ++index) added += addMovesOfChart(table, others[index].chart, numbers);
    if (numbers.skipped > 0 && logger) {
        logger->write("WARN", "a stance has no free move number left; some moves of its other stances were left out.");
    }
    if (added == 0) return false;
    rebuildFirstMoves(table);
    return true;
}

// [esp+4] = table, edi = chart; returns al (the chart has cards), ret 4.
__declspec(naked) void addChosenMoves() {
    __asm {
        push dword ptr [esp + 4]
        call originalAddChosenMoves
        push eax
        push edi
        push dword ptr [esp + 0xc]  // The table, past eax, edi and the return address
        call addMovesOfOtherCharts
        add esp, 8
        pop ecx
        or al, cl
        ret 4
    }
}

// --- Installing -------------------------------------------------------------------------------

CodePatch patches[] = {
    // mov eax,[ebp-0x4c]; movsx ecx,ax -> call addPointsOfOtherCharts; nop (set at install)
    CodePatch(0x5c9ff7, {0xe8, 0, 0, 0, 0, 0x90}),
    // mov edi,[esp+0xc]; xor ecx,ecx -> call checkCardBeforeChoosing; nop (set at install)
    CodePatch(0x5cb832, {0xe8, 0, 0, 0, 0, 0x90}),
};
const void* const patchTargets[] = {&addPointsOfOtherCharts, &checkCardBeforeChoosing};

void setCallBytes(const GameImage& game) {
    constexpr uint32_t callLength = 5;
    for (size_t index = 0; index < _countof(patches); ++index) {
        const uintptr_t from = game.address(patches[index].preferredAddress());
        const auto offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(patchTargets[index]) - (from + callLength));
        uint8_t call[] = {0xe8, 0, 0, 0, 0, 0x90};
        std::memcpy(call + 1, &offset, sizeof(offset));
        patches[index].setPatchedBytes(call);
    }
}

bool cardCodeMatches(const GameImage& game) {
    const uint8_t setState[] = {0x89, 0x8e, 0x1c, 0x02, 0x00, 0x00};     // 0x65ffb9 mov [esi+0x21c],ecx
    const uint8_t firstParent[] = {0x39, 0x81, 0x2c, 0x02, 0x00, 0x00};  // 0x660f52 cmp [ecx+0x22c],eax
    return game.codeIs(0x65ffb9, setState, sizeof(setState)) &&
           game.codeIs(0x660f52, firstParent, sizeof(firstParent)) && game.callsTo(0x5cb896, 0x658900) &&
           game.callsTo(0x657456, 0x657b10) && game.callsTo(0x658add, 0x65ffa0);
}

// The code this part relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches(const GameImage& game) {
    const uint8_t total[] = {0x8b, 0x45, 0xb4, 0x0f, 0xbf, 0xc8, 0x8d, 0x5d, 0xbc};  // ...; lea ebx,[ebp-0x44]
    const uint8_t cardCost[] = {0x0f, 0xbf, 0x92, 0x56, 0x02, 0x00, 0x00};          // 0x5c9fd1
    const uint8_t message[] = {0x8b, 0x7c, 0x24, 0x0c, 0x33, 0xc9, 0x66, 0x3b, 0x0f};  // ...; cmp cx,[edi]
    const uint8_t messageOfScreen[] = {0x8d, 0x83, 0xbc, 0x03, 0x00, 0x00};           // 0x5cb6fd
    const uint8_t command[] = {0x66, 0x3b, 0x82, 0x54, 0x02, 0x00, 0x00};             // 0x6587e5
    const uint8_t messageKind[] = {0xc7, 0x45, 0xcc, 0x04, 0x00, 0x00, 0x00};         // 0x5d3b7e
    const uint8_t keepWindow[] = {0x89, 0x9a, 0xd0, 0x01, 0x00, 0x00};                // 0x5d3bca
    const uint8_t noAttackText[] = {0xbf, 0xd5, 0x02, 0x00, 0x00};                    // 0x5d3b62, UI 725
    return game.codeIs(0x5c9ff7, total, sizeof(total)) && game.codeIs(0x5c9fd1, cardCost, sizeof(cardCost)) &&
           game.codeIs(0x5cb832, message, sizeof(message)) &&
           game.codeIs(0x5cb6fd, messageOfScreen, sizeof(messageOfScreen)) &&
           game.codeIs(0x6587e5, command, sizeof(command)) && game.codeIs(0x5d3b7e, messageKind, sizeof(messageKind)) &&
           game.codeIs(0x5d3bca, keepWindow, sizeof(keepWindow)) &&
           game.codeIs(0x5d3b62, noAttackText, sizeof(noAttackText)) && game.callsTo(0x5d33ad, 0x5d3660) &&
           game.callsTo(0x5d372e, 0x598df0) && game.callsTo(0x5d3744, 0x599430) &&
           game.callsTo(0x5d3b59, 0x5d3c00) && game.callsTo(0x5d2e66, 0x5d3ab0) &&
           game.callsTo(0x5d3c88, 0x59d9b0) && game.callsTo(0x6586be, 0x59da20) && game.operandIs(0x5cbf8e, 0x9ebb14) &&
           cardCodeMatches(game);
}

struct RulesHook {
    uintptr_t preferred;
    void* detour;
    void** original;
};
const RulesHook hooks[] = {
    {0x5d3660, reinterpret_cast<void*>(&addChosenMoves), &originalAddChosenMoves},
    {0x5d3c00, reinterpret_cast<void*>(&everySlotHasAttack), reinterpret_cast<void**>(&originalEverySlotHasAttack)},
    {0x5d3ab0, reinterpret_cast<void*>(&refuseStyle), reinterpret_cast<void**>(&originalRefuseStyle)},
    {0x658900, reinterpret_cast<void*>(&letGoCard), reinterpret_cast<void**>(&originalLetGoCard)},
    {0x657b10, reinterpret_cast<void*>(&resetChart), reinterpret_cast<void**>(&originalResetChart)},
};

}  // namespace

bool installStanceRules(const wots4::ModContext& context, const GameImage& game, const TweakLog& log) {
    if (!codeMatches(game)) {
        log.installFailed("the My Style creation code is not the known version", 0x5d3660);
        return false;
    }
    host = context.host;
    logger = &log;
    setCallBytes(game);
    if (!claimPatches(context, game, patches, _countof(patches), log)) return false;
    constexpr uint32_t hookLength = 5;
    for (size_t index = 0; index < _countof(hooks); ++index) {
        CodeChangeError error =
            hookGameCode(context, game, hooks[index].preferred, hookLength, hooks[index].detour, hooks[index].original);
        if (!error) continue;
        for (size_t done = 0; done < index; ++done) unhookGameCode(game, hooks[done].preferred);
        log.installFailed(error, hooks[index].preferred);
        return false;
    }
    return true;
}

void dimCardsTakenElsewhere(void* chart) {
    dimTakenCards(chart);
}

void uninstallStanceRules(const GameImage& game) {
    for (const RulesHook& hook : hooks) unhookGameCode(game, hook.preferred);
}

void setStanceRulesEnabled(bool enabled, const TweakLog& log) {
    writePatches(patches, _countof(patches), enabled, log);
}

}  // namespace tweaks::custom_style_stance
