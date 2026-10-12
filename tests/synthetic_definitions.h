// Synthetic game objects for the Laura night visit tests: a person definition table laid out
// as 0x43c250 reads it, a game state with the promise, and a night visit manager.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>
#include "../src/laura_yobai/laura_mark.h"

namespace synthetic {

using namespace tweaks::laura_yobai;

constexpr int32_t lauraDefinition = 454;   // Laura_01
constexpr int32_t otherDefinition = 108;   // Y_Partner_01, not Laura
constexpr int32_t missingDefinition = 300;  // Inside the table but with offset 0
constexpr int32_t definitionCount = 800;
constexpr uint32_t definitionSize = 0x40;
constexpr uint32_t gameStateSize = promisedDefinitionOffset + 4;
constexpr uint32_t managerSize = 0xac;
constexpr int16_t otherSpecialNumber = 5;

struct GameObjects {
    std::vector<uint8_t> table;
    const uint8_t* tableHolder;  // [holder] = table
    std::vector<uint8_t> definitionRoot;
    std::vector<uint8_t> gameState = std::vector<uint8_t>(gameStateSize);
    std::vector<uint8_t> manager = std::vector<uint8_t>(managerSize);

    GameObjects() {
        const uint32_t offsetsEnd = definitionOffsetsStart + definitionCount * 4;
        table.resize(offsetsEnd + 2 * definitionSize);
        writeInt32(table.data(), definitionCount);
        addDefinition(lauraDefinition, offsetsEnd, lauraSpecialNumber);
        addDefinition(otherDefinition, offsetsEnd + definitionSize, otherSpecialNumber);

        tableHolderStorage = table.data();
        tableHolder = reinterpret_cast<const uint8_t*>(&tableHolderStorage);
        definitionRoot.resize(definitionTableHolderOffset + 4);
        writeInt32(definitionRoot.data(), definitionRootWithTable);
        std::memcpy(definitionRoot.data() + definitionTableHolderOffset, &tableHolder, 4);
        promise(noPromise);
    }
    // tableHolder points into this object.
    GameObjects(const GameObjects&) = delete;
    GameObjects& operator=(const GameObjects&) = delete;

    void promise(int32_t definition) { writeInt32(gameState.data() + promisedDefinitionOffset, definition); }
    uint8_t& mark() { return manager[lauraMarkOffset]; }

private:
    const uint8_t* tableHolderStorage = nullptr;

    static void writeInt32(uint8_t* at, int32_t value) { std::memcpy(at, &value, 4); }

    void addDefinition(int32_t id, uint32_t offset, int16_t specialNumber) {
        writeInt32(table.data() + definitionOffsetsStart + id * 4, static_cast<int32_t>(offset));
        std::memcpy(table.data() + offset + specialNumberOffset, &specialNumber, 2);
    }
};

}  // namespace synthetic
