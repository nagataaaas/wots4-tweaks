// Reads the game's code and globals. Addresses in this Mod are the exe's preferred ones
// (image base 0x400000); ASLR moves the image, so every access goes through address().
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace tweaks {

class GameImage {
public:
    GameImage() = default;
    explicit GameImage(HMODULE game) : base(reinterpret_cast<uintptr_t>(game)) {}

    uintptr_t address(uintptr_t preferred) const { return base + preferred - preferredBase; }

    bool codeIs(uintptr_t preferred, const uint8_t* bytes, size_t count) const {
        return std::memcmp(reinterpret_cast<const void*>(address(preferred)), bytes, count) == 0;
    }

    // The 32-bit value at preferred: a game global, or an operand of an instruction.
    uint32_t readUint32(uintptr_t preferred) const {
        return *reinterpret_cast<const uint32_t*>(address(preferred));
    }

    // True if the operand at operandAddress holds the relocated address of the global.
    bool operandIs(uintptr_t operandAddress, uintptr_t global) const {
        return readUint32(operandAddress) == address(global);
    }

    // True if the call (e8 rel32) at callAddress goes to target.
    bool callsTo(uintptr_t callAddress, uintptr_t target) const {
        constexpr uint8_t callOpcode = 0xe8;
        constexpr uintptr_t callLength = 5;
        if (*reinterpret_cast<const uint8_t*>(address(callAddress)) != callOpcode) return false;
        const auto offset = static_cast<int32_t>(readUint32(callAddress + 1));
        return callAddress + callLength + offset == target;
    }

private:
    static constexpr uintptr_t preferredBase = 0x400000;
    uintptr_t base = preferredBase;
};

}  // namespace tweaks
