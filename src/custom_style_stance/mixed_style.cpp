#include "mixed_style.h"
#include "game_calls.h"
#include "my_style_screen.h"

namespace tweaks::custom_style_stance {

namespace {

// A style's move table, made from a stance's SkillComboData or, for a My Style, from the moves
// chosen for one of its slots.
struct StyleTable {
    static constexpr uint32_t kind = 0x2;        // uint8, 2 for a My Style (0x462c11)
    static constexpr uint32_t stance = 0x4;      // int16 (0x44b95a, 0x59844f)
    static constexpr uint32_t moveCount = 0x1c;  // int (0x59a151)
};
constexpr uint8_t myStyleKind = 2;

// A move of a table, as tableMoveAt returns it.
struct StyleMove {
    static constexpr uint32_t number = 0x0;  // int16
    static constexpr uint32_t skill = 0x2;   // int16, SkillData ID
};

constexpr uint32_t comboMoveCount = 0x10;  // int (0x44b9b3)

struct Character {
    static constexpr uint32_t motionStore = 0x4;     // Where motions are registered (0x44ba67)
    static constexpr uint32_t motionStates = 0x15c;  // int per slot key k (0x44b977)
};
constexpr int motionsLoading = 1;
constexpr int motionsRegistered = 2;
constexpr int slotKeyLimit = 64;  // The keys in use are 22-39 (0x97dd38)

constexpr int moveLimit = 128;  // Move numbers are below 128

void* stanceComboOf(void* table) {
    return comboOf(field<int16_t>(table, StyleTable::stance));
}

// The SkillData of the moves put in from other stances: those under numbers that the table's
// stance does not have (addMovesOfOtherCharts).
int otherStanceMoves(void* table, void* (&skillData)[moveLimit]) {
    if (field<uint8_t>(table, StyleTable::kind) != myStyleKind) return 0;
    void* combo = stanceComboOf(table);
    if (!combo) return 0;
    const int count = field<int>(table, StyleTable::moveCount);
    int found = 0;
    for (int index = 0; index < count && found < moveLimit; ++index) {
        void* move = tableMoveAt(table, index);
        if (!move) break;
        if (comboHasNumber(combo, field<int16_t>(move, StyleMove::number))) continue;
        void* data = skillDataOf(field<int16_t>(move, StyleMove::skill));
        if (data) skillData[found++] = data;
    }
    return found;
}

// --- Motions ----------------------------------------------------------------------------------

void* originalRegisterMotions;

// ecx = table, edi = character; loader, k on the stack (ret 8). Returns 0 until registered.
uint32_t callOriginalRegisterMotions(void* table, void* character, void* loader, int slotKey) {
    uint32_t result;
    __asm {
        push edi
        push slotKey
        push loader
        mov edi, character
        mov ecx, table
        call originalRegisterMotions
        mov result, eax
        pop edi
    }
    return result;
}

void registerOtherMotions(void* character, void* loader, int slotKey, void* const* skillData, int count) {
    MotionEntries entries;
    for (int index = 0; index < count; ++index) addMotionEntries(loader, skillData[index], entries);
    void* store = field<void*>(character, Character::motionStore);
    for (uint8_t* entry = entries.begin; entry < entries.end; entry += motionEntrySize) {
        registerMotion(store, slotKey, entry);
    }
    freeMotionEntries(entries);
}

// 0x44b8b0 loads and registers the motions of the moves of the table's stance (its combo), not
// of the table's own moves. The other stances' moves are loaded alongside and registered under
// the same k once the game's are, so the game's clean-up (0x497f80) removes them with its own.
// Called each frame until it returns non-zero, then once per equip; the registered state makes
// the later calls return at once.
uint32_t __cdecl registerMotions(void* table, void* character, void* loader, int slotKey) {
    if (!table || !character || !loader || slotKey < 0 || slotKey >= slotKeyLimit) {
        return callOriginalRegisterMotions(table, character, loader, slotKey);
    }
    int& state = field<int>(character, Character::motionStates + slotKey * sizeof(int));
    if (state == motionsRegistered) return callOriginalRegisterMotions(table, character, loader, slotKey);
    void* skillData[moveLimit];
    const int count = otherStanceMoves(table, skillData);
    if (count == 0) return callOriginalRegisterMotions(table, character, loader, slotKey);
    bool ready = true;
    for (int index = 0; index < count; ++index) loadMotions(loader, skillData[index], ready);
    const uint32_t registered = callOriginalRegisterMotions(table, character, loader, slotKey);
    if (!registered) return 0;
    if (!ready) {
        // The game's would not be registered again once marked registered.
        state = motionsLoading;
        return 0;
    }
    registerOtherMotions(character, loader, slotKey, skillData, count);
    return registered;
}

// ecx = table, edi = character; loader, k on the stack (ret 8)
__declspec(naked) void registerMotionsWithOthers() {
    __asm {
        push dword ptr [esp + 8]  // k
        push dword ptr [esp + 8]  // loader, past the k pushed
        push edi
        push ecx
        call registerMotions
        add esp, 0x10
        ret 8
    }
}

// --- Per-stance record --------------------------------------------------------------------------

// What 0x598400 reads of a table: only its stance (+4).
struct StanceOnlyTable {
    uint8_t unused[4];
    int16_t stance;
};
StanceOnlyTable stanceTables[stanceCount];

// Where a move is kept in its stance's record: the stance and the move's number there.
struct StanceRecord {
    void* table;
    int number;
};

// Where each SkillData ID is in the stances' combos, found once: the combos do not change while
// the game runs. Ids at or past the limit are looked up each time.
constexpr int cachedSkillLimit = 4096;
constexpr int16_t notInCombos = -1;
int16_t homeOfSkill[cachedSkillLimit];  // stance x moveLimit + number
bool homesFound = false;

int16_t searchHome(int skill) {
    for (int stance = 0; stance < stanceCount; ++stance) {
        void* combo = comboOf(stance);
        if (!combo) continue;
        const int count = field<int>(combo, comboMoveCount);
        for (int index = 0; index < count; ++index) {
            void* move = comboMoveAt(combo, index);
            if (!move) break;
            if (field<int16_t>(move, ComboEntry::skill) != skill) continue;
            const int number = field<int16_t>(move, ComboEntry::number);
            if (number >= 0 && number < moveLimit) return static_cast<int16_t>(stance * moveLimit + number);
        }
    }
    return notInCombos;
}

bool findHomes() {
    for (int stance = 0; stance < stanceCount; ++stance) {
        if (!comboOf(stance)) return false;  // Not loaded yet; looked for again next time
    }
    for (int16_t& home : homeOfSkill) home = notInCombos;
    for (int stance = 0; stance < stanceCount; ++stance) {
        void* combo = comboOf(stance);
        const int count = field<int>(combo, comboMoveCount);
        for (int index = 0; index < count; ++index) {
            void* move = comboMoveAt(combo, index);
            if (!move) break;
            const int skill = field<int16_t>(move, ComboEntry::skill);
            const int number = field<int16_t>(move, ComboEntry::number);
            if (skill < 0 || skill >= cachedSkillLimit || number < 0 || number >= moveLimit) continue;
            if (homeOfSkill[skill] == notInCombos) homeOfSkill[skill] = static_cast<int16_t>(stance * moveLimit + number);
        }
    }
    return true;
}

int16_t homeOf(int skill) {
    if (skill < 0) return notInCombos;
    if (skill >= cachedSkillLimit) return searchHome(skill);
    if (!homesFound) homesFound = findHomes();
    return homesFound ? homeOfSkill[skill] : searchHome(skill);
}

int skillOfNumber(void* table, int number) {
    const int count = field<int>(table, StyleTable::moveCount);
    for (int index = 0; index < count; ++index) {
        void* move = tableMoveAt(table, index);
        if (!move) break;
        if (field<int16_t>(move, StyleMove::number) == number) return field<int16_t>(move, StyleMove::skill);
    }
    return -1;
}

// 0x598400 records a move of a table as seen (1) or used (2) under the table's stance and the
// move's number. A move put in from another stance has a number its stance does not use there,
// so it is recorded under its own stance and number instead. False to record nothing (a move
// in no combo).
bool __cdecl findStanceRecord(void* table, int number, StanceRecord* record) {
    record->table = table;
    record->number = number;
    if (!table || field<uint8_t>(table, StyleTable::kind) != myStyleKind) return true;
    void* combo = stanceComboOf(table);
    if (!combo || comboHasNumber(combo, number)) return true;
    const int16_t home = homeOf(skillOfNumber(table, number));
    if (home == notInCombos) return false;
    record->table = &stanceTables[home / moveLimit];
    record->number = home % moveLimit;
    return true;
}

void* originalRecordMove;

// esi = save data, edi = table, ebx = number; the state on the stack (cdecl).
__declspec(naked) void recordMoveUnderItsStance() {
    __asm {
        sub esp, 8  // StanceRecord
        mov eax, esp
        push eax
        push ebx
        push edi
        call findStanceRecord
        add esp, 0xc
        test al, al
        jz done
        push edi
        push ebx
        mov edi, dword ptr [esp + 8]     // record.table
        mov ebx, dword ptr [esp + 0xc]   // record.number
        push dword ptr [esp + 0x14]      // The state, past ebx, edi, the record and the return address
        call originalRecordMove
        add esp, 4
        pop ebx
        pop edi
    done:
        add esp, 8
        ret
    }
}

// --- Installing -------------------------------------------------------------------------------

// The code this part relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches(const GameImage& game) {
    const uint8_t readState[] = {0x8b, 0x84, 0x87, 0x5c, 0x01, 0x00, 0x00};  // 0x44b977
    const uint8_t writeRegistered[] = {0xc7, 0x84, 0x97, 0x5c, 0x01, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
    const uint8_t comboCount[] = {0x8b, 0x46, 0x10};                    // 0x44b9b3 mov eax,[esi+0x10]
    const uint8_t store[] = {0x8b, 0x47, 0x04};                         // 0x44ba67 mov eax,[edi+4]
    const uint8_t recordStance[] = {0x0f, 0xbf, 0x47, 0x04};            // 0x59844f movsx eax,[edi+4]
    const uint8_t recordAt[] = {0x8d, 0x8c, 0x1a, 0x94, 0x83, 0x01, 0x00};  // 0x598464
    const uint8_t styleKind[] = {0x80, 0x7a, 0x02, 0x02};               // 0x462c11 cmp byte [edx+2],2
    const uint8_t tableCount[] = {0x8b, 0x48, 0x1c};                    // 0x59a151 mov ecx,[eax+0x1c]
    return game.codeIs(0x44b977, readState, sizeof(readState)) &&
           game.codeIs(0x44ba91, writeRegistered, sizeof(writeRegistered)) &&
           game.codeIs(0x44b9b3, comboCount, sizeof(comboCount)) && game.codeIs(0x44ba67, store, sizeof(store)) &&
           game.codeIs(0x59844f, recordStance, sizeof(recordStance)) &&
           game.codeIs(0x598464, recordAt, sizeof(recordAt)) && game.codeIs(0x462c11, styleKind, sizeof(styleKind)) &&
           game.codeIs(0x59a151, tableCount, sizeof(tableCount)) && game.operandIs(0x44b8eb, 0x9ebb10) &&
           game.callsTo(0x44b9cb, 0x597a60) && game.callsTo(0x44b9dd, 0x5980a0) &&
           game.callsTo(0x44b9f0, 0x44bb90) && game.callsTo(0x44ba49, 0x44bc50) &&
           game.callsTo(0x44ba70, 0x454c70) && game.callsTo(0x44b9a7, 0x44bea0) &&
           game.callsTo(0x59a173, 0x599170) && game.callsTo(0x59a516, 0x598400);
}

struct MixedStyleHook {
    uintptr_t preferred;
    void* detour;
    void** original;
};
const MixedStyleHook hooks[] = {
    {0x44b8b0, reinterpret_cast<void*>(&registerMotionsWithOthers), &originalRegisterMotions},
    {0x598400, reinterpret_cast<void*>(&recordMoveUnderItsStance), &originalRecordMove},
};

}  // namespace

bool installMixedStyle(const wots4::ModContext& context, const GameImage& game, const TweakLog& log) {
    if (!codeMatches(game)) {
        log.installFailed("the motion registration code is not the known version", 0x44b8b0);
        return false;
    }
    for (int stance = 0; stance < stanceCount; ++stance) stanceTables[stance].stance = static_cast<int16_t>(stance);
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

void uninstallMixedStyle(const GameImage& game) {
    for (const MixedStyleHook& hook : hooks) unhookGameCode(game, hook.preferred);
}

}  // namespace tweaks::custom_style_stance
