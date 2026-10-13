// Installs mod.dll on a copy of the exe mapped into this process and checks the stance button
// of the My Style move select menu: the patched bytes and the copied layout list, the label of
// the fifth button, the button animations, the menu's cursor, the list of stances, switching
// the shown stance, the style's rules over all the charts of a slot (points, 706 / 744, 725,
// the moves put into the style) and giving the charts back. The game functions that draw, play
// sounds, open the choice window, allocate, free or add moves are replaced by stand-ins that
// record their calls.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

// Game addresses
constexpr uintptr_t layoutSlots = 0x5ca10c;
constexpr uintptr_t layoutListSet = 0x5ca10f;
constexpr uintptr_t labelLoopEnd = 0x5ca945;
constexpr uintptr_t labelText = 0x5ca877;
constexpr uintptr_t labelTextReturn = 0x5ca87d;
constexpr uintptr_t placeLoopEnd = 0x5caf10;
constexpr uintptr_t highlightLoopEnd = 0x5caf34;
constexpr uintptr_t placeButtons = 0x5ca460;
constexpr uintptr_t highlightButton = 0x5cb070;
constexpr uintptr_t highlightCursor = 0x5cb110;
constexpr uintptr_t menuInput = 0x5cb1a0;
constexpr uintptr_t releaseShared = 0x5c9ea0;
constexpr uintptr_t gameLayoutList = 0x979664;
// Stand-ins replace these
constexpr uintptr_t playAnimationAtOnce = 0x5ba380;
constexpr uintptr_t playAnimation = 0x5ba580;
constexpr uintptr_t playMenuSound = 0x5a0050;
constexpr uintptr_t allocate = 0x6c7020;
constexpr uintptr_t constructChart = 0x657280;
constexpr uintptr_t buildShownChart = 0x5cbef0;
constexpr uintptr_t stanceName = 0x6861b0;
constexpr uintptr_t refreshCardInformation = 0x5cc150;
constexpr uintptr_t freeImport = 0x9052c0;
constexpr uintptr_t clearChoiceWindow = 0x5bda60;
constexpr uintptr_t fillChoiceWindow = 0x5be410;
constexpr uintptr_t showChoiceWindow = 0x5bddf0;
constexpr uintptr_t choiceWindowHolder = 0x9ebb8c;  // [[0x9ebb8c]+0x70] is the window
// Game globals the menu reads
constexpr uintptr_t decideKeys = 0x9db500;
constexpr uintptr_t cancelKeys = 0x9db504;
constexpr uintptr_t upKeys = 0x9db5cc;
constexpr uintptr_t downKeys = 0x9db5d0;
constexpr uintptr_t menuBlockedHolder = 0x9ecf30;  // [[0x9ecf30]+0x1c01] != 0 blocks the cursor
constexpr uint32_t decideKey = 0x10, cancelKey = 0x20, upKey = 0x40, downKey = 0x80;

// Screen and shared data offsets
constexpr uint32_t screenLayoutList = 0x8, screenShared = 0x34, screenTitle = 0x3c, screenCursor = 0x43c,
                   screenChart = 0x440, screenState = 0x444, screenDialog = 0x468, screenAnimation = 0x46c;
constexpr uint32_t sharedStances = 0x48, sharedSlot = 0x4e, sharedCharts = 0x5c;
constexpr uint32_t ownerShared = 0xc8;

template <typename T>
T& at(void* object, uint32_t offset) {
    return *reinterpret_cast<T*>(static_cast<uint8_t*>(object) + offset);
}

// What the stand-ins saw
int animationsPlayed, animationsAtOnce, lastAnimation, lastAnimationAtOnce;
int chartsBuilt, chartsDeleted;
void* lastBuiltChart;
int16_t lastBuiltStance;
std::vector<void*> deletedCharts;
uint8_t chartPool[16][0x9c];
int chartsAllocated;
const wchar_t* const stanceNames[13] = {L"Jo-dan", L"Chu-dan", L"Ge-dan", L"Waki", L"Iai", L"One-Handed",
    L"Shinobi", L"Spear", L"Dual Arms", L"Shinobi Dual Arms", L"Hand-to-Hand", L"Pistol", L"Rifle"};

void __cdecl noteChartDeleted(void* chart) {
    ++chartsDeleted;
    deletedCharts.push_back(chart);
}

__declspec(naked) void fakePlayAnimation() {  // eax = animation
    __asm {
        mov lastAnimation, eax
        inc animationsPlayed
        ret
    }
}
__declspec(naked) void fakePlayAnimationAtOnce() {  // ecx = animation
    __asm {
        mov lastAnimationAtOnce, ecx
        inc animationsAtOnce
        ret
    }
}
__declspec(naked) void fakeReturn() {
    __asm { ret }
}
int cursorSounds, lastSound;
__declspec(naked) void fakePlayMenuSound() {  // edx = sound
    __asm {
        mov lastSound, edx
        inc cursorSounds
        ret
    }
}
__declspec(naked) void fakeReturn4() {
    __asm { ret 4 }
}
void* __cdecl fakeAllocate(uint32_t, uint32_t) {
    return chartsAllocated < 16 ? chartPool[chartsAllocated++] : nullptr;
}
void* __fastcall fakeDeleteChart(void* chart, void*, uint32_t) {
    noteChartDeleted(chart);
    return chart;
}
void* fakeChartMethods[1] = {reinterpret_cast<void*>(&fakeDeleteChart)};
void* __stdcall fakeConstructChart(void* memory) {
    std::memset(memory, 0, 0x9c);
    *static_cast<void***>(memory) = fakeChartMethods;
    return memory;
}
void __cdecl noteBuild(void* screen) {
    ++chartsBuilt;
    lastBuiltChart = at<void*>(screen, screenChart);
    void* shared = at<void*>(screen, screenShared);
    lastBuiltStance = at<int16_t>(shared, sharedStances + 2 * at<int16_t>(shared, sharedSlot));
}
__declspec(naked) void fakeBuildShownChart() {  // eax = screen
    __asm {
        push eax
        call noteBuild
        add esp, 4
        ret
    }
}
const wchar_t* __cdecl nameOf(int stance) {
    return stance >= 0 && stance < 13 ? stanceNames[stance] : nullptr;
}
__declspec(naked) void fakeStanceName() {  // edx = stance
    __asm {
        push ecx
        push edx
        call nameOf
        add esp, 4
        pop ecx
        ret
    }
}
void __cdecl fakeFree(void*) {}

// The choice window: what it was filled with, and its state.
constexpr uint32_t windowState = 0x38, windowChosen = 0xa0, windowOpenState = 2, windowClosedState = 4;
uint8_t fakeWindow[0x100];
uint8_t fakeWindowHolder[0x80];
int windowsCleared, windowsShown;
uint32_t filledKind;
int filledCursor;
std::vector<std::pair<uint32_t, uint32_t>> filledChoices;
void __stdcall fakeClearChoiceWindow(void*) {
    ++windowsCleared;
}
void __cdecl noteFill(void* window, const uint8_t* settings) {
    filledKind = *reinterpret_cast<const uint32_t*>(settings);
    filledCursor = static_cast<int8_t>(settings[0xc]);
    filledChoices.clear();
    const auto* choice = *reinterpret_cast<const uint32_t* const*>(settings + 0x10);
    const auto* end = *reinterpret_cast<const uint32_t* const*>(settings + 0x14);
    for (; choice < end; choice += 2) filledChoices.emplace_back(choice[0], choice[1]);
    at<int>(window, windowState) = windowOpenState;
}
__declspec(naked) void fakeFillChoiceWindow() {  // ecx = window, edx = settings
    __asm {
        push edx
        push ecx
        call noteFill
        add esp, 8
        ret
    }
}
__declspec(naked) void fakeShowChoiceWindow() {  // eax = window
    __asm {
        inc windowsShown
        ret
    }
}

void jumpTo(uintptr_t preferred, const void* target) {
    codeByte(preferred) = jumpOpcode;
    *reinterpret_cast<int32_t*>(va(preferred + 1)) =
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (va(preferred) + 5));
}

uint32_t& gameGlobal(uintptr_t preferred) { return *reinterpret_cast<uint32_t*>(va(preferred)); }

uintptr_t jumpTarget(uintptr_t preferred) {
    return va(preferred) + 5 + *reinterpret_cast<const int32_t*>(va(preferred + 1));
}

// Runs the label stand-in the draw loop jumps to, with label number i and the game's label.
const wchar_t* labelFor(uintptr_t thunk, int i, const wchar_t* gameLabel) {
    const uint8_t savedByte = codeByte(labelTextReturn);
    codeByte(labelTextReturn) = returnOpcode;
    const wchar_t* label;
    __asm {
        push esi
        sub esp, 0x24
        mov eax, gameLabel
        mov dword ptr [esp + 0xc], eax  // [esp+0x10] after the call pushes its return address
        mov eax, i
        mov dword ptr [esp + 0x20], eax  // [esp+0x24]
        call thunk
        add esp, 0x24
        mov label, esi
        pop esi
    }
    codeByte(labelTextReturn) = savedByte;
    return label;
}

using ThisCall0 = void(__thiscall*)(void*);
using ThisCall2 = void(__thiscall*)(void*, uint32_t, uint32_t);
using ThisCall3 = void(__thiscall*)(void*, int, uint32_t, uint32_t);

void menu(void* screen, uint32_t pressed, uint32_t repeated) {
    at<int>(screen, screenAnimation) = -1;  // The game clears it when the animation ends
    reinterpret_cast<ThisCall2>(va(menuInput))(screen, pressed, repeated);
}

void release(void* owner) {
    const uintptr_t function = va(releaseShared);
    __asm {
        push esi
        mov esi, owner
        call function
        pop esi
    }
}

std::wstring titleOf(void* screen) { return &at<wchar_t>(screen, screenTitle); }

// The step the screen calls each frame: the menu, or the wait for the list of stances.
void step(void* screen) {
    at<int>(screen, screenAnimation) = -1;
    reinterpret_cast<ThisCall2>(at<uintptr_t>(screen, screenState))(screen, 0, 0);
}

// Decides the stance button, then closes the list with the choice (-1 = cancelled).
void chooseFromList(void* screen, int chosen) {
    at<int>(screen, screenCursor) = 4;
    menu(screen, decideKey, 0);
    at<int>(fakeWindow, windowState) = windowClosedState;
    at<int>(fakeWindow, windowChosen) = chosen;
    step(screen);
}

bool listedTexts(std::initializer_list<uint32_t> numbers) {
    if (filledChoices.size() != numbers.size()) return false;
    size_t index = 0;
    for (uint32_t number : numbers) {
        if (filledChoices[index].first != 5 || filledChoices[index].second != number) return false;
        ++index;
    }
    return true;
}

// --- The style's rules: cards, combo data and the creation's stand-ins ---

constexpr uintptr_t totalPoints = 0x5c9f80;       // Runs for real
constexpr uintptr_t messageCheck = 0x5cb832;      // Calls the tweak
constexpr uintptr_t addChosenMoves = 0x5d3660;    // Hooked; the game's part runs for real
constexpr uintptr_t everySlotHasAttack = 0x5d3c00;
constexpr uintptr_t refuseStyle = 0x5d3ab0;
constexpr uintptr_t debugAssert = 0x504c80;
constexpr uintptr_t listFirstMoveCards = 0x6574a0;
constexpr uintptr_t addMove = 0x598df0;
constexpr uintptr_t rebuildFirstMoves = 0x599430;
constexpr uintptr_t numberText = 0x684d50;
constexpr uintptr_t joinTexts = 0x684f00;
constexpr uintptr_t defenceIndex = 0x59d9b0;
constexpr uintptr_t defenceType = 0x59da20;
constexpr uintptr_t comboHolder = 0x9ebb14;
constexpr uintptr_t letGoCard = 0x658900;   // Hooked; the game's part runs for real
constexpr uintptr_t resetChart = 0x657b10;  // Hooked; the game's part runs for real
constexpr uintptr_t cardLooks[] = {0x660940, 0x660a60, 0x660ad0};  // Register arguments, ret
constexpr uintptr_t cardLook4 = 0x6607c0;                           // ret 4
constexpr uint32_t sharedPoints = 0x50, sharedPointLimit = 0x52, topShared = 0x34, topWindow = 0x1d0;
constexpr uint32_t screenMessage = 0x3bc;

struct FakeEntry {
    int16_t number, skill;
    uint8_t unused4[2], mark, unused7, linkCount, unused9[3];
    const int16_t* links;
};
struct FakeCard {
    uint8_t unused[0x21c];
    int state;
    uint8_t unused220[0x2c];
    void* otherEntry;
    FakeEntry* entry;
    uint16_t command;
    int16_t cost;
};
static_assert(offsetof(FakeCard, entry) == 0x250 && offsetof(FakeCard, cost) == 0x256, "card layout");
static_assert(offsetof(FakeEntry, links) == 0xc, "entry layout");

void setCards(void* chart, std::vector<FakeCard*>& cards) {
    at<FakeCard**>(chart, 4) = cards.data();
    at<FakeCard**>(chart, 8) = cards.data() + cards.size();
}

// Defence moves: SkillData 300 and 301, both of type 1.
int __cdecl fakeDefenceIndex(int skill) { return skill == 300 ? 0 : skill == 301 ? 1 : -1; }
int __cdecl fakeDefenceType(int skill) { return skill == 300 || skill == 301 ? 1 : -1; }
__declspec(naked) void fakeNoBreak() {  // The debug assert: never break
    __asm {
        xor eax, eax
        ret
    }
}

struct AddedMove {
    int number, skill, mark;
    std::vector<int> links;
};
std::vector<AddedMove> addedMoves;
int firstMovesRebuilt;
void __cdecl noteMove(void*, const int8_t* linkCount, const int16_t* number, const int16_t* skill,
    const uint8_t* mark, const int16_t* links) {
    addedMoves.push_back({*number, *skill, *mark, std::vector<int>(links, links + *linkCount)});
}
__declspec(naked) void fakeAddMove() {  // ecx = table, eax = &link count; &number, &skill, &mark, links
    __asm {
        push dword ptr [esp + 0x10]
        push dword ptr [esp + 0x10]
        push dword ptr [esp + 0x10]
        push dword ptr [esp + 0x10]
        push eax
        push ecx
        call noteMove
        add esp, 0x18
        mov eax, 1
        ret 0x10
    }
}
__declspec(naked) void fakeRebuildFirstMoves() {
    __asm {
        inc firstMovesRebuilt
        ret
    }
}

// The combo tree of a stance: a chain to the higher side (+0xc) with the keys given.
struct FakeNode {
    uint8_t unused[8];
    FakeNode* lower;
    FakeNode* higher;
    int16_t key;
    uint8_t unused12[2];
    void* entry;
};
uint8_t combos[13][36];
FakeNode comboNodes[8];
uintptr_t comboLevel2, comboLevel1;
void setComboNumbers(int stance, std::initializer_list<int16_t> numbers) {
    FakeNode** link = reinterpret_cast<FakeNode**>(combos[stance] + 4);
    int index = 0;
    for (int16_t number : numbers) {
        FakeNode& node = comboNodes[index++];
        node.key = number;
        node.entry = &node;
        *link = &node;
        link = &node.higher;
    }
}

bool addChosen(void* table, void* chart) {
    const uintptr_t function = va(addChosenMoves);
    uint32_t result;
    __asm {
        push edi
        mov edi, chart
        push table
        call function
        movzx eax, al
        mov result, eax
        pop edi
    }
    return result != 0;
}

// Runs the check the move select makes before choosing, with the screen's message.
void checkBeforeChoosing(wchar_t* message) {
    const uintptr_t thunk = jumpTarget(messageCheck);
    __asm {
        push edi
        sub esp, 0x10
        mov eax, message
        mov dword ptr [esp + 0xc], eax
        call thunk
        add esp, 0x10
        pop edi
    }
}

// --- Created styles: motions and the per-stance record ---

constexpr uintptr_t registerMotions = 0x44b8b0;      // Hooked
constexpr uintptr_t registerMotionsBody = 0x44b8b6;  // Past the prologue the hook copies
constexpr uintptr_t recordMove = 0x598400;           // Hooked; the game's part runs for real
constexpr uintptr_t comboMoveAt = 0x597a60;
constexpr uintptr_t tableMoveAt = 0x599170;
constexpr uintptr_t skillDataHolder = 0x9ebb10;
constexpr uintptr_t skillDataOf = 0x5980a0;
constexpr uintptr_t loadMotions = 0x44bb90;
constexpr uintptr_t addMotionEntries = 0x44bc50;
constexpr uintptr_t registerMotion = 0x454c70;
constexpr uintptr_t freeMotionEntries = 0x44bea0;
constexpr uint32_t motionStates = 0x15c, motionStore = 0x4, stanceRecords = 0x18394;

// The game's part of 0x44b8b0, past its prologue: registered at once when the state says so,
// else registered when gameMotionsReady, else marked loading. Returns the character or 0.
int gameRegistrations;
bool gameMotionsReady;
__declspec(naked) void fakeRegisterTail() {  // ecx = table, edi = character; [ebp+8] loader, [ebp+0xc] k
    __asm {
        mov eax, dword ptr [ebp + 0xc]
        cmp dword ptr [edi + eax * 4 + 0x15c], 2
        je registered
        inc gameRegistrations
        cmp gameMotionsReady, 0
        je loading
        mov dword ptr [edi + eax * 4 + 0x15c], 2
    registered:
        mov eax, edi
        jmp done
    loading:
        mov dword ptr [edi + eax * 4 + 0x15c], 1
        xor eax, eax
    done:
        mov esp, ebp
        pop ebp
        ret 8
    }
}

uint8_t skillDataPool[1024];  // SkillData ID n stands at skillDataPool + n
int skillOfData(const void* skillData) { return static_cast<int>(static_cast<const uint8_t*>(skillData) - skillDataPool); }
__declspec(naked) void fakeSkillDataOf() {  // eax = holder; ID on the stack (ret 4)
    __asm {
        mov eax, dword ptr [esp + 4]
        add eax, offset skillDataPool
        ret 4
    }
}

std::vector<int> loadedSkills;
bool motionsPending;
void __cdecl noteLoad(const void* skillData, bool* ready) {
    loadedSkills.push_back(skillOfData(skillData));
    if (motionsPending) *ready = false;
}
__declspec(naked) void fakeLoadMotions() {  // ecx = loader; SkillData, &ready (ret 8)
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call noteLoad
        add esp, 8
        ret 8
    }
}

struct FakeEntries {
    uint8_t* begin;
    uint8_t* end;
    uint8_t* capacity;
};
uint8_t entryPool[64][0x10];
int entriesUsed;
void __cdecl noteEntries(const void* skillData, FakeEntries* entries) {
    uint8_t* entry = entryPool[entriesUsed++ % 64];
    *reinterpret_cast<int*>(entry) = skillOfData(skillData);  // Stands for the motion
    if (!entries->begin) entries->begin = entries->end = entry;
    if (entries->end != entry) return;  // The pool wrapped: not contiguous any more
    entries->end = entry + 0x10;
}
__declspec(naked) void fakeAddMotionEntries() {  // ecx = loader; SkillData, &entries (ret 8)
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call noteEntries
        add esp, 8
        ret 8
    }
}

struct RegisteredMotion {
    int key, skill;
    void* store;
};
std::vector<RegisteredMotion> registeredMotions;
void __cdecl noteRegistered(int key, const int* entry, void* store) { registeredMotions.push_back({key, *entry, store}); }
__declspec(naked) void fakeRegisterMotion() {  // eax = k, ebx = entry; store (ret 4)
    __asm {
        push dword ptr [esp + 4]
        push ebx
        push eax
        call noteRegistered
        add esp, 0xc
        ret 4
    }
}

int entriesFreed;
__declspec(naked) void fakeFreeMotionEntries() {  // &entries (ret 4)
    __asm {
        inc entriesFreed
        ret 4
    }
}

std::vector<FakeEntry*> styleMoves;
void* __cdecl styleMoveAt(int index) {
    return index < static_cast<int>(styleMoves.size()) ? styleMoves[index] : nullptr;
}
__declspec(naked) void fakeTableMoveAt() {  // eax = table, edi = index
    __asm {
        push edi
        call styleMoveAt
        add esp, 4
        ret
    }
}

std::map<const void*, std::vector<FakeEntry*>> comboMoves;
void* __cdecl comboMove(const void* combo, const int* index) {
    auto found = comboMoves.find(combo);
    if (found == comboMoves.end() || *index >= static_cast<int>(found->second.size())) return nullptr;
    return found->second[*index];
}
__declspec(naked) void fakeComboMoveAt() {  // esi = combo, ecx = &index
    __asm {
        push ecx
        push esi
        call comboMove
        add esp, 8
        ret
    }
}

uint32_t registerWithGame(void* table, void* character, void* loader, int key) {
    const uintptr_t function = va(registerMotions);
    uint32_t result;
    __asm {
        push edi
        push key
        push loader
        mov ecx, table
        mov edi, character
        call function
        mov result, eax
        pop edi
    }
    return result;
}

void recordWithGame(void* saveData, void* table, int number, int state) {
    const uintptr_t function = va(recordMove);
    __asm {
        push esi
        push edi
        push ebx
        mov esi, saveData
        mov edi, table
        mov ebx, number
        push state
        call function
        add esp, 4
        pop ebx
        pop edi
        pop esi
    }
}

using TopCheck = bool(__stdcall*)(void*);
using ChartCardCall = void(__stdcall*)(void*, void*);
using ChartCall = void(__stdcall*)(void*);

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_custom_style_stance")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;
    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(layoutSlots, 3) && claimed(layoutListSet, 7) && claimed(labelLoopEnd, 3) &&
              claimed(labelText, 6) && claimed(placeLoopEnd, 3) && claimed(highlightLoopEnd, 3),
        "the six menu patches are claimed");
    check(claimed(placeButtons, 5) && claimed(highlightButton, 5) && claimed(highlightCursor, 5) &&
              claimed(menuInput, 5) && claimed(releaseShared, 5),
        "the five hooks are claimed");
    check(claimed(0x5c9ff7, 6) && claimed(0x5cb832, 6) && claimed(0x5d3660, 5) && claimed(0x5d3c00, 5) &&
              claimed(0x5d3ab0, 5) && claimed(letGoCard, 5) && claimed(resetChart, 5),
        "the rules' two patches and five hooks are claimed");
    check(claimed(registerMotions, 5) && claimed(recordMove, 5), "the created styles' two hooks are claimed");
    const uint32_t item = optionIndex(L"Mixed-Stance My Style");
    check(item != noItem && std::wstring(optionItem(item).label.japanese) == L"俺流構え混合",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");
    check(codeByte(layoutSlots + 2) == 5 && *reinterpret_cast<uint32_t*>(va(layoutListSet + 3)) == va(gameLayoutList) &&
              codeByte(labelLoopEnd + 2) == 4 && codeByte(labelText) == 0x8b && codeByte(placeLoopEnd + 2) == 0x14 &&
              codeByte(highlightLoopEnd + 2) == 4,
        "off: the menu's code is unchanged");

    changeOption(item, 1);
    auto* list = reinterpret_cast<uint8_t*>(*reinterpret_cast<uint32_t*>(va(layoutListSet + 3)));
    check(codeByte(layoutSlots + 2) == 6 && list != reinterpret_cast<uint8_t*>(va(gameLayoutList)) &&
              codeByte(labelLoopEnd + 2) == 5 && codeByte(placeLoopEnd + 2) == 0x18 && codeByte(highlightLoopEnd + 2) == 5,
        "on: six layouts from the tweak's list, and the loops go over five buttons");
    const auto* layouts = at<const char*>(list, 0x8);
    const auto* animations = at<const uint8_t*>(list, 0x10);
    check(at<uint32_t>(list, 0xc) == 6 && std::strcmp(layouts + 5 * 0x40, "wazaFunc.layout") == 0,
        "the list has a sixth layout, another wazaFunc.layout");
    check(at<uint32_t>(list, 0x14) == 11 &&
              std::strcmp(reinterpret_cast<const char*>(animations + 9 * 0x6c + 8), "releace") == 0 &&
              std::strcmp(reinterpret_cast<const char*>(animations + 10 * 0x6c + 8), "select") == 0 &&
              at<int>(const_cast<uint8_t*>(animations), 9 * 0x6c + 0x68) == 5 &&
              at<int>(const_cast<uint8_t*>(animations), 10 * 0x6c + 0x68) == 5 &&
              at<int>(const_cast<uint8_t*>(animations), 8 * 0x6c + 0x68) == 4,
        "animations 9 and 10 play on the sixth layout, the game's on theirs");

    check(codeByte(labelText) == jumpOpcode && codeByte(labelText + 5) == 0x90, "the label text jumps to the tweak");
    const uintptr_t thunk = jumpTarget(labelText);
    const wchar_t gameLabel[] = L"Auto-Select";
    check(std::wstring(labelFor(thunk, 5, gameLabel)) == L"Switch Stance",
        "label 5 is \"Switch Stance\" (English when the game's text is not at hand)");
    check(labelFor(thunk, 2, gameLabel) == gameLabel, "labels 1-4 are the game's");

    // Stand-ins for what draws, plays sounds, allocates and frees.
    jumpTo(playAnimation, &fakePlayAnimation);
    jumpTo(playAnimationAtOnce, &fakePlayAnimationAtOnce);
    jumpTo(playMenuSound, &fakePlayMenuSound);
    jumpTo(allocate, &fakeAllocate);
    jumpTo(constructChart, &fakeConstructChart);
    jumpTo(buildShownChart, &fakeBuildShownChart);
    jumpTo(stanceName, &fakeStanceName);
    jumpTo(refreshCardInformation, &fakeReturn4);
    gameGlobal(freeImport) = reinterpret_cast<uint32_t>(&fakeFree);
    jumpTo(clearChoiceWindow, &fakeClearChoiceWindow);
    jumpTo(fillChoiceWindow, &fakeFillChoiceWindow);
    jumpTo(showChoiceWindow, &fakeShowChoiceWindow);
    at<void*>(fakeWindowHolder, 0x70) = fakeWindow;
    gameGlobal(choiceWindowHolder) = reinterpret_cast<uint32_t>(fakeWindowHolder);
    gameGlobal(decideKeys) = decideKey;
    gameGlobal(cancelKeys) = cancelKey;
    gameGlobal(upKeys) = upKey;
    gameGlobal(downKeys) = downKey;
    static uint8_t menuBlocked[0x2000] = {};
    gameGlobal(menuBlockedHolder) = reinterpret_cast<uint32_t>(menuBlocked);

    static uint8_t screen[0x500] = {};
    static uint8_t shared[0x100] = {};
    static uint8_t gameChart[0x9c] = {};
    fakeConstructChart(gameChart);
    at<void*>(screen, screenLayoutList) = list;
    at<void*>(screen, screenShared) = shared;
    at<void*>(screen, screenChart) = gameChart;
    at<uint32_t>(screen, screenState) = static_cast<uint32_t>(va(menuInput));
    at<int16_t>(shared, sharedStances) = 1;  // Chu-dan in slot 0
    at<int16_t>(shared, sharedStances + 2) = -1;
    at<int16_t>(shared, sharedStances + 4) = -1;
    at<int16_t>(shared, sharedSlot) = 0;
    at<void*>(shared, sharedCharts) = gameChart;

    // Animations: button i plays 1 + 2i when chosen, 2 + 2i otherwise.
    at<int>(screen, screenCursor) = 4;
    reinterpret_cast<ThisCall0>(va(highlightCursor))(screen);
    check(animationsPlayed == 5 && lastAnimation == 9 && at<int>(screen, screenAnimation) == 9,
        "the cursor highlight goes over five buttons; the stance button under the cursor plays 9");
    reinterpret_cast<ThisCall3>(va(highlightButton))(screen, 4, 0, 1);
    check(animationsAtOnce == 1 && lastAnimationAtOnce == 10, "the stance button not chosen plays 10 at once");
    reinterpret_cast<ThisCall3>(va(highlightButton))(screen, 0, 1, 0);
    check(lastAnimation == 1, "button 0 is left to the game (1 when chosen)");
    at<void*>(screen, screenLayoutList) = reinterpret_cast<void*>(va(gameLayoutList));
    animationsPlayed = 0;
    reinterpret_cast<ThisCall0>(va(highlightCursor))(screen);
    check(animationsPlayed == 4, "a screen made with the game's list keeps four buttons");
    at<void*>(screen, screenLayoutList) = list;

    // The cursor, through the game's menu code: Move Select, Switch Stance, Auto-Select, Copy
    // Stance, Finish from left to right.
    at<int>(screen, screenCursor) = 0;
    animationsPlayed = 0;
    menu(screen, 0, downKey);
    check(at<int>(screen, screenCursor) == 4, "next from Move Select is the stance button");
    check(animationsPlayed == 5 && lastAnimation == 9 && cursorSounds == 1 && lastSound == 0x61,
        "one move highlights the five buttons once and plays the cursor sound once");
    menu(screen, 0, downKey);
    check(at<int>(screen, screenCursor) == 1, "next from the stance button is Auto-Select");
    menu(screen, 0, upKey);
    check(at<int>(screen, screenCursor) == 4, "previous from Auto-Select is the stance button");
    menu(screen, 0, upKey);
    check(at<int>(screen, screenCursor) == 0, "previous from the stance button is Move Select");
    menu(screen, 0, upKey);
    check(at<int>(screen, screenCursor) == 3, "previous from Move Select wraps to Finish");
    menu(screen, 0, downKey);
    check(at<int>(screen, screenCursor) == 0, "next from Finish wraps to Move Select");
    at<int>(screen, screenCursor) = 1;
    menu(screen, 0, downKey);
    check(at<int>(screen, screenCursor) == 2, "Auto-Select to Copy Stance is the game's step");
    at<int>(screen, screenCursor) = 4;
    at<int>(screen, screenAnimation) = 2;
    reinterpret_cast<ThisCall2>(va(menuInput))(screen, decideKey, 0);
    check(windowsShown == 0, "nothing happens while a button animation plays");

    // The list of stances: Chu-dan's group is the sword stances 0-6.
    animationsAtOnce = 0;
    menu(screen, decideKey, 0);
    check(windowsCleared == 1 && windowsShown == 1 && filledKind == 2 && filledCursor == 1 &&
              listedTexts({112, 113, 114, 115, 116, 117, 118, 59}),
        "deciding the stance button opens a list of Jo-dan .. Shinobi and Quit, on Chu-dan");
    check(at<void*>(screen, 0x448) == fakeWindow && at<uintptr_t>(screen, screenState) != va(menuInput) &&
              animationsAtOnce == 1 && lastAnimationAtOnce == 10 && chartsBuilt == 0,
        "the screen waits for the list; the stance button is let go (10)");
    step(screen);
    check(at<uintptr_t>(screen, screenState) != va(menuInput) && chartsBuilt == 0,
        "nothing happens while the list is open");
    at<int>(fakeWindow, windowState) = windowClosedState;
    at<int>(fakeWindow, windowChosen) = 2;
    step(screen);
    check(at<uintptr_t>(screen, screenState) == va(menuInput) && at<void*>(screen, 0x448) == nullptr &&
              lastAnimationAtOnce == 9,
        "once the list closes, the menu takes the input again with the stance button chosen (9)");
    check(chartsBuilt == 1 && lastBuiltStance == 2 && lastBuiltChart == chartPool[0],
        "choosing Ge-dan fills a new chart with Ge-dan's cards");
    check(at<void*>(screen, screenChart) == chartPool[0] && at<void*>(shared, sharedCharts) == chartPool[0] &&
              titleOf(screen) == L"Ge-dan" && at<int16_t>(shared, sharedStances) == 1,
        "the Ge-dan chart is shown and edited in place of the slot's, the slot stays Chu-dan");
    chooseFromList(screen, 0);
    check(filledCursor == 2 && chartsBuilt == 2 && titleOf(screen) == L"Jo-dan" &&
              at<void*>(screen, screenChart) == chartPool[1],
        "the list opens on the stance shown; Jo-dan gets a chart of its own");
    chooseFromList(screen, 1);
    check(at<void*>(screen, screenChart) == gameChart && at<void*>(shared, sharedCharts) == gameChart &&
              titleOf(screen) == L"Chu-dan" && chartsBuilt == 2,
        "choosing the slot's own stance shows the game's chart");
    chooseFromList(screen, 2);
    check(chartsBuilt == 2 && at<void*>(screen, screenChart) == chartPool[0],
        "Ge-dan again shows the chart made before, with its chosen cards");
    chooseFromList(screen, -1);
    check(at<void*>(screen, screenChart) == chartPool[0] && at<uintptr_t>(screen, screenState) == va(menuInput),
        "cancelling the list keeps the stance shown");
    chooseFromList(screen, 7);
    check(at<void*>(screen, screenChart) == chartPool[0] && titleOf(screen) == L"Ge-dan", "Quit keeps it too");

    menu(screen, cancelKey, 0);
    check(at<void*>(screen, screenChart) == gameChart && at<void*>(shared, sharedCharts) == gameChart &&
              titleOf(screen) == L"Chu-dan",
        "leaving the menu gives the game its chart and title back");

    at<int16_t>(shared, sharedSlot) = 0;
    at<int16_t>(shared, sharedStances) = 8;  // Dual Arms
    chooseFromList(screen, 1);
    check(listedTexts({120, 121, 59}) && filledCursor == 0, "dual arms list Dual Arms, Shinobi Dual Arms and Quit");
    check(chartsDeleted == 2 && lastBuiltStance == 9 && titleOf(screen) == L"Shinobi Dual Arms",
        "a new slot stance drops the old charts; Shinobi Dual Arms is shown");
    chooseFromList(screen, 0);
    check(at<void*>(screen, screenChart) == gameChart && titleOf(screen) == L"Dual Arms", "and back to Dual Arms");
    at<int16_t>(shared, sharedStances) = 7;  // Spear
    const int shownBefore = windowsShown;
    at<int>(screen, screenCursor) = 4;
    menu(screen, decideKey, 0);
    check(windowsShown == shownBefore && at<uintptr_t>(screen, screenState) == va(menuInput),
        "the spear has no other stance: no list opens");

    // The style's rules. Slot 0 is Chu-dan; the game's chart has A (first move, command 0x10)
    // leading to B, and the defence move H; Ge-dan's chart has C (first move) leading to D and
    // E, F (first move, command 0x10) and the defence move G of H's type.
    jumpTo(debugAssert, &fakeNoBreak);
    jumpTo(listFirstMoveCards, &fakeReturn4);
    jumpTo(addMove, &fakeAddMove);
    jumpTo(rebuildFirstMoves, &fakeRebuildFirstMoves);
    jumpTo(numberText, &fakeReturn);
    jumpTo(joinTexts, &fakeReturn);
    jumpTo(defenceIndex, &fakeDefenceIndex);
    jumpTo(defenceType, &fakeDefenceType);
    for (uintptr_t look : cardLooks) jumpTo(look, &fakeReturn);
    jumpTo(cardLook4, &fakeReturn4);
    comboLevel2 = reinterpret_cast<uintptr_t>(combos);
    comboLevel1 = reinterpret_cast<uintptr_t>(&comboLevel2);
    gameGlobal(comboHolder) = reinterpret_cast<uint32_t>(&comboLevel1);
    setComboNumbers(1, {0, 1, 2, 3, 5});  // Chu-dan's numbers; 4, 6, 7, ... are free
    gameTexts[706] = L"First move command is set to a different move.";
    gameTexts[744] = L"Cannot set two defensive moves of the same type.";

    const int16_t toOne[] = {1};
    const int16_t toOneAndSeven[] = {1, 7};
    FakeEntry entryA = {0, 100, {}, 1, 0, 1, {}, toOne}, entryB = {1, 101}, entryH = {2, 300};
    FakeEntry entryC = {0, 200, {}, 1, 0, 2, {}, toOneAndSeven}, entryD = {1, 201}, entryE = {7, 202},
              entryF = {3, 203, {}, 1}, entryG = {4, 301};
    static FakeCard cardA, cardB, cardH, cardC, cardD, cardE, cardF, cardG;
    const auto makeCard = [](FakeCard& card, FakeEntry& entry, uint16_t command, int16_t cost, int state) {
        card.entry = &entry;
        card.command = command;
        card.cost = cost;
        card.state = state;
    };
    makeCard(cardA, entryA, 0x10, 30, 1);
    makeCard(cardB, entryB, 0x11, 20, 1);
    makeCard(cardH, entryH, 0x12, 5, 0);
    makeCard(cardC, entryC, 0x20, 40, 1);
    makeCard(cardD, entryD, 0x21, 10, 1);
    makeCard(cardE, entryE, 0x22, 3, 0);
    makeCard(cardF, entryF, 0x10, 8, 0);
    makeCard(cardG, entryG, 0x23, 7, 1);
    std::vector<FakeCard*> gameCards = {&cardA, &cardB, &cardH};
    std::vector<FakeCard*> geDanCards = {&cardC, &cardD, &cardE, &cardF, &cardG};
    static uint8_t emptyCharts[2][0x9c] = {};
    at<int16_t>(shared, sharedStances) = 1;
    at<void*>(shared, sharedCharts + 4) = emptyCharts[0];  // The game adds up all three slots
    at<void*>(shared, sharedCharts + 8) = emptyCharts[1];
    setCards(gameChart, gameCards);
    chooseFromList(screen, 2);  // Shows Ge-dan
    void* geDanChart = at<void*>(screen, screenChart);
    setCards(geDanChart, geDanCards);

    reinterpret_cast<ThisCall0>(va(totalPoints))(shared);
    check(at<int16_t>(shared, sharedPoints) == 30 + 20 + 40 + 10 + 7,
        "the ability points add up the chosen cards of Ge-dan's chart shown and the game's chart");
    chooseFromList(screen, 1);
    reinterpret_cast<ThisCall0>(va(totalPoints))(shared);
    check(at<int16_t>(shared, sharedPoints) == 107, "and the same with the game's chart shown");
    changeOption(item, 0);
    reinterpret_cast<ThisCall0>(va(totalPoints))(shared);
    check(at<int16_t>(shared, sharedPoints) == 50 && codeByte(messageCheck) == 0x8b,
        "off: the game adds up its own charts only, and the move select's code is its own");
    changeOption(item, 1);

    wchar_t* message = &at<wchar_t>(screen, screenMessage);
    chooseFromList(screen, 2);
    check(cardF.state == 2 && cardC.state == 1 && cardE.state == 0,
        "showing Ge-dan dims F: A on the game's chart takes its command");
    at<void*>(geDanChart, 0x68) = &cardF;
    wcscpy_s(message, 64, gameTexts[706].c_str());  // As the game explains a dimmed first move
    checkBeforeChoosing(message);
    check(std::wstring(message) == gameTexts[706] + L" (Chu-dan)",
        "deciding the dimmed F gives 706 with the stance that takes the command");
    cardF.state = 0;
    message[0] = 0;
    checkBeforeChoosing(message);
    check(std::wstring(message) == gameTexts[706] + L" (Chu-dan)", "a free F is refused the same way");
    at<void*>(geDanChart, 0x68) = &cardE;
    message[0] = 0;
    checkBeforeChoosing(message);
    check(message[0] == 0, "E, no first move, may be chosen");
    at<void*>(geDanChart, 0x68) = &cardF;
    wcscpy_s(message, 64, L"701");
    checkBeforeChoosing(message);
    check(std::wstring(message) == L"701", "a free card's message written before (points) is kept");
    at<void*>(geDanChart, 0x68) = &cardC;
    message[0] = 0;
    checkBeforeChoosing(message);
    check(message[0] == 0, "a chosen card is let go without a check");
    cardF.state = 2;

    reinterpret_cast<ChartCall>(va(resetChart))(geDanChart);
    check(cardC.state == 0 && cardD.state == 2 && cardG.state == 0 && cardF.state == 2,
        "a reset of Ge-dan (Auto-Select, Copy Stance) frees its first moves but F, taken by A");
    cardC.state = cardD.state = cardG.state = 1;
    cardE.state = 0;
    reinterpret_cast<ChartCardCall>(va(letGoCard))(geDanChart, &cardC);
    check(cardC.state == 0 && cardF.state == 2, "letting go C keeps F dimmed");
    cardC.state = 1;
    cardA.state = 0;
    chooseFromList(screen, 1);
    chooseFromList(screen, 2);
    check(cardF.state == 0, "with A let go, showing Ge-dan again brightens F");
    cardA.state = 1;
    chooseFromList(screen, 1);
    chooseFromList(screen, 2);
    check(cardF.state == 2, "and with A chosen again F is dimmed again");

    chooseFromList(screen, 1);
    at<void*>(gameChart, 0x68) = &cardH;
    message[0] = 0;
    checkBeforeChoosing(message);
    check(std::wstring(message) == gameTexts[744] + L" (Ge-dan)",
        "choosing H on the game's chart is refused with 744 and Ge-dan, where G of its type is chosen");
    check(cardH.state == 0, "H, a defence move but no first move, is not dimmed (the game would say 705)");

    static uint8_t top[0x200] = {};
    at<void*>(top, topShared) = shared;
    at<int16_t>(shared, sharedPointLimit) = 0x7fff;
    const auto attackCheck = reinterpret_cast<TopCheck>(va(everySlotHasAttack));
    const auto refuse = reinterpret_cast<TopCheck>(va(refuseStyle));
    cardA.state = 0;
    cardB.state = 0;
    cardH.state = 1;
    cardG.state = 0;
    check(attackCheck(top), "a slot whose attack moves are all on another stance's chart has attack moves");
    cardC.state = 0;
    cardD.state = 0;
    check(!attackCheck(top), "a slot with only a defence move chosen has none (725)");
    cardA.state = cardB.state = cardC.state = cardD.state = 1;
    cardH.state = 0;
    cardG.state = 1;
    const int shownBefore2 = windowsShown;
    check(!refuse(top) && windowsShown == shownBefore2, "a style without clashing cards is not refused");
    cardF.state = 1;  // As Auto-Select could choose it
    check(refuse(top) && windowsShown == shownBefore2 + 1 && filledKind == 4 && listedTexts({706}) &&
              at<void*>(top, topWindow) == fakeWindow,
        "a style with A and F chosen is refused with the message 706");
    cardF.state = 0;

    static uint8_t table[0x40] = {};
    table[2] = 2;  // A style's table
    addedMoves.clear();
    firstMovesRebuilt = 0;
    const bool hasCards = addChosen(table, gameChart);
    check(hasCards && addedMoves.size() == 5 && addedMoves[0].number == 0 && addedMoves[0].links == std::vector<int>{1} &&
              addedMoves[1].number == 1,
        "creating the style puts in the game's A and B, then three moves of Ge-dan");
    check(addedMoves[2].number == 4 && addedMoves[2].skill == 200 && addedMoves[2].mark == 1 &&
              addedMoves[2].links == std::vector<int>{6},
        "C takes Chu-dan's free number 4, and leads to D only (E is not chosen)");
    check(addedMoves[3].number == 6 && addedMoves[3].skill == 201 && addedMoves[3].links.empty() &&
              addedMoves[4].number == 7 && addedMoves[4].skill == 301,
        "D takes 6 and G takes 7, past Chu-dan's 5");
    check(firstMovesRebuilt == 2, "the list of first moves is made again after Ge-dan's moves");
    static uint8_t strayChart[0x9c] = {};
    fakeConstructChart(strayChart);
    addedMoves.clear();
    addChosen(table, strayChart);
    check(addedMoves.empty(), "a chart of no slot gets no other stance's moves");

    // A created Chu-dan style with Chu-dan's 0 and, from Ge-dan, C (Ge-dan 0) as 4 and D
    // (Ge-dan 1) as 6.
    jumpTo(registerMotionsBody, &fakeRegisterTail);
    jumpTo(comboMoveAt, &fakeComboMoveAt);
    jumpTo(tableMoveAt, &fakeTableMoveAt);
    jumpTo(skillDataOf, &fakeSkillDataOf);
    jumpTo(loadMotions, &fakeLoadMotions);
    jumpTo(addMotionEntries, &fakeAddMotionEntries);
    jumpTo(registerMotion, &fakeRegisterMotion);
    jumpTo(freeMotionEntries, &fakeFreeMotionEntries);
    gameGlobal(skillDataHolder) = reinterpret_cast<uint32_t>(skillDataPool);
    FakeEntry styleA = {0, 100}, styleC = {4, 200}, styleD = {6, 201};
    styleMoves = {&styleA, &styleC, &styleD};
    comboMoves[combos[2]] = {&entryC, &entryD};
    at<int>(combos[2], 0x10) = 2;
    static uint8_t style[0x40] = {};
    style[2] = 2;
    at<int16_t>(style, 4) = 1;
    at<int>(style, 0x1c) = 3;
    static uint8_t character[0x200] = {};
    static uint8_t store[0x10] = {};
    static uint8_t loader[0x10] = {};
    at<void*>(character, motionStore) = store;
    constexpr int slotKey = 22;
    int& motionState = at<int>(character, motionStates + slotKey * 4);

    motionsPending = true;
    gameMotionsReady = true;
    uint32_t registered = registerWithGame(style, character, loader, slotKey);
    check(registered == 0 && motionState == 1 && loadedSkills == std::vector<int>{200, 201} && registeredMotions.empty(),
        "while C's and D's motions load, the style is not registered, though the game's are ready");
    motionsPending = false;
    loadedSkills.clear();
    registered = registerWithGame(style, character, loader, slotKey);
    check(registered == reinterpret_cast<uint32_t>(character) && motionState == 2 && gameRegistrations == 2 &&
              registeredMotions.size() == 2 && registeredMotions[0].skill == 200 && registeredMotions[1].skill == 201 &&
              registeredMotions[0].key == slotKey && registeredMotions[1].store == store && entriesFreed == 1,
        "once loaded, C's and D's motions are registered under the slot's k, after the game's");
    loadedSkills.clear();
    registered = registerWithGame(style, character, loader, slotKey);
    check(registered == reinterpret_cast<uint32_t>(character) && loadedSkills.empty() && registeredMotions.size() == 2,
        "a registered slot is left to the game");
    motionState = 0;
    style[2] = 1;  // A stance's own table
    registeredMotions.clear();
    registerWithGame(style, character, loader, slotKey);
    check(loadedSkills.empty() && registeredMotions.empty() && motionState == 2, "a stance's table is left to the game");
    style[2] = 2;
    motionState = 0;
    changeOption(item, 0);
    registerWithGame(style, character, loader, slotKey);
    check(registeredMotions.size() == 2, "off, a style made before still gets its moves' motions");
    changeOption(item, 1);

    static uint8_t saveData[stanceRecords + 0x80 * 13] = {};
    const auto record = [](int stance, int number) { return saveData[stanceRecords + 0x80 * stance + number]; };
    recordWithGame(saveData, style, 0, 1);
    check(record(1, 0) == 1, "Chu-dan's own move is recorded under Chu-dan");
    recordWithGame(saveData, style, 4, 1);
    recordWithGame(saveData, style, 6, 2);
    check(record(2, 0) == 1 && record(2, 1) == 2 && record(1, 4) == 0 && record(1, 6) == 0,
        "C and D are recorded under Ge-dan 0 and 1, not under Chu-dan's 4 and 6");
    recordWithGame(saveData, style, 9, 1);
    int written = 0;
    for (uint8_t value : saveData) written += value != 0;
    check(written == 3, "a number the style does not have is not recorded");
    style[2] = 1;
    recordWithGame(saveData, style, 4, 1);
    check(record(1, 4) == 1, "a stance's table records as the game does");
    style[2] = 2;

    at<void*>(shared, sharedCharts + 4) = nullptr;
    at<void*>(shared, sharedCharts + 8) = nullptr;
    chooseFromList(screen, 2);  // Shows Ge-dan
    static uint8_t owner[0x100] = {};
    at<void*>(owner, ownerShared) = shared;
    deletedCharts.clear();
    const int deletedBefore = chartsDeleted;
    release(owner);
    bool gameChartDeleted = false;
    for (void* chart : deletedCharts) gameChartDeleted |= chart == gameChart;
    check(chartsDeleted == deletedBefore + 2 && gameChartDeleted && at<void*>(owner, ownerShared) == nullptr,
        "closing the screens deletes the tweak's chart and lets the game delete its own");

    return finish("custom_style_stance_native_probe");
}
