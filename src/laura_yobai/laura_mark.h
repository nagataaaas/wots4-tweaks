// Decides the "Laura mark" of the night visit (yobai) manager ([0x9ecbb4] +0xa8) from the
// promised partner. With the mark set, the person in the futon looks like Laura and turns
// into a sailor when the futon is lifted; without it, the visit is with Laura herself.
// The game sets the mark only when a promise is made and clears it only when the night visit
// managers are created, so after restarting the game, or after loading another save, the
// mark no longer matches the promise.
#pragma once
#include <cstdint>

namespace tweaks::laura_yobai {

// The option's values, in the order of its choices.
enum PartnerSetting : uint32_t {
    partnerAsTheGameDecides = 0,
    partnerFake = 1,  // Always the sailor
    partnerReal = 2,  // Always Laura
};

// int16 at +6 of a person definition. Laura's definitions (43, 454, 789) have 0x13; making
// a promise sets the mark exactly when the partner has it (0x69aa71, 0x69abda).
constexpr uint32_t specialNumberOffset = 0x6;
constexpr int16_t lauraSpecialNumber = 0x13;

constexpr uint32_t lauraMarkOffset = 0xa8;              // uint8 in the night visit manager
constexpr uint32_t promisedDefinitionOffset = 0x14054;  // int32 in the game state [0x9e967c]
constexpr int32_t noPromise = -1;

// The definition root ([[0x9e9640]], read through two pointers) holds the person definitions when its first int32 is 4
// (0x6a0291); then +0x24 points to the table pointer.
constexpr int32_t definitionRootWithTable = 4;
constexpr uint32_t definitionTableHolderOffset = 0x24;
// Table: int32 count, then from +0xc one int32 offset per ID from the table to the definition.
constexpr uint32_t definitionOffsetsStart = 0xc;

constexpr uint8_t markUnset = 0;
constexpr uint8_t markSet = 1;

template <typename T>
T readAt(const uint8_t* address) {
    return *reinterpret_cast<const T*>(address);
}

// The person definition with the ID, found as 0x6a028a and 0x43c250 do, or nullptr.
inline const uint8_t* findPersonDefinition(const uint8_t* definitionRoot, int32_t id) {
    if (!definitionRoot || id < 0) return nullptr;
    if (readAt<int32_t>(definitionRoot) != definitionRootWithTable) return nullptr;
    const auto* holder = readAt<const uint8_t*>(definitionRoot + definitionTableHolderOffset);
    if (!holder) return nullptr;
    const auto* table = readAt<const uint8_t*>(holder);
    if (!table || readAt<int32_t>(table) <= id) return nullptr;
    const auto offset = readAt<int32_t>(table + definitionOffsetsStart + static_cast<uint32_t>(id) * 4);
    return offset > 0 ? table + offset : nullptr;
}

inline bool fixesPartner(uint32_t setting) {
    return setting == partnerFake || setting == partnerReal;
}

// The mark for a setting that fixes the partner: only the fake partner sets it, and only for Laura.
inline uint8_t lauraMarkFor(uint32_t setting, bool partnerIsLaura) {
    return setting == partnerFake && partnerIsLaura ? markSet : markUnset;
}

struct MarkUpdate {
    bool written;
    uint8_t before;
    uint8_t after;
    int32_t promisedDefinition;
};

// Sets the mark of the manager for the promised partner. Writes nothing when the setting keeps
// the game's value, or the manager, the promise or its definition is missing.
inline MarkUpdate updateLauraMark(uint32_t setting, uint8_t* manager, const uint8_t* gameState,
    const uint8_t* definitionRoot) {
    MarkUpdate update{false, 0, 0, noPromise};
    if (!manager || !gameState) return update;
    update.before = update.after = manager[lauraMarkOffset];
    update.promisedDefinition = readAt<int32_t>(gameState + promisedDefinitionOffset);

    if (!fixesPartner(setting)) return update;
    const uint8_t* definition = findPersonDefinition(definitionRoot, update.promisedDefinition);
    if (!definition) return update;

    const bool partnerIsLaura = readAt<int16_t>(definition + specialNumberOffset) == lauraSpecialNumber;
    update.after = lauraMarkFor(setting, partnerIsLaura);
    manager[lauraMarkOffset] = update.after;
    update.written = true;
    return update;
}

}  // namespace tweaks::laura_yobai
