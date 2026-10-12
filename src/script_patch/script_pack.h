// Reads the action script pack (Common/ActionScript/Script/AsScriptPack.l) as the game keeps it
// in memory once loaded. The game's commands read their arguments from this data every time
// they run (0x41e470), so changing an argument here changes what the script does.
//
// Pack:        int32 script count, then per script {int32 ID, int32 offset from the pack}
// Script:      int32 function count, int32 offset of the argument words, then per function
//              {int32 offset of its instructions, uint16 instruction count, 2 bytes}
// Instruction: 16 bytes; +0 uint8 argument count, +3 uint8 bits of ignored arguments,
//              +0xa uint16 command, +0xc uint32 offset of its first argument word
#pragma once
#include <cstdint>

namespace tweaks::script_patch {

constexpr uint32_t instructionSize = 0x10;
// Command 35 jumps to one of its first arguments, chosen with rand(); command 36 compares
// and jumps to its argument 0 (the label) when the comparison holds.
constexpr uint16_t randomJumpCommand = 35;
constexpr uint16_t compareJumpCommand = 36;
constexpr uint32_t maxRandomTargets = 8;

template <typename T>
T readAt(const uint8_t* address) {
    return *reinterpret_cast<const T*>(address);
}

inline uint8_t* findScript(uint8_t* pack, int32_t id) {
    const int32_t count = readAt<int32_t>(pack);
    for (int32_t index = 0; index < count; ++index) {
        const uint8_t* entry = pack + 4 + index * 8;
        if (readAt<int32_t>(entry) == id) return pack + readAt<int32_t>(entry + 4);
    }
    return nullptr;
}

inline uint32_t functionCount(const uint8_t* script) { return readAt<uint32_t>(script); }

inline uint32_t instructionCount(const uint8_t* script, uint32_t function) {
    return readAt<uint16_t>(script + 8 + function * 8 + 4);
}

inline const uint8_t* instructionAt(const uint8_t* script, uint32_t function, uint32_t index) {
    return script + readAt<uint32_t>(script + 8 + function * 8) + index * instructionSize;
}

inline uint16_t commandOf(const uint8_t* instruction) { return readAt<uint16_t>(instruction + 0xa); }

inline int16_t* argumentWords(uint8_t* script, const uint8_t* instruction) {
    return reinterpret_cast<int16_t*>(script + readAt<uint32_t>(script + 4) + readAt<uint32_t>(instruction + 0xc));
}

// How many targets command 35 chooses from: arguments up to the first ignored one (0x410b49).
inline uint32_t randomTargetCount(const uint8_t* instruction) {
    const uint8_t argumentCount = instruction[0];
    const uint8_t ignored = instruction[3];
    uint32_t count = 0;
    while (count < maxRandomTargets && count < argumentCount && !(ignored & (1u << count))) ++count;
    return count;
}

// The argument words that are jump targets: command 35's choices, or command 36's label.
inline uint32_t jumpTargetCount(const uint8_t* instruction) {
    if (commandOf(instruction) == randomJumpCommand) return randomTargetCount(instruction);
    if (commandOf(instruction) == compareJumpCommand) return 1;
    return 0;
}

}  // namespace tweaks::script_patch
