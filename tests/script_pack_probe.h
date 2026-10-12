// Runs the game's registration of an action script pack on the mapped exe copy with a copy of
// the real pack in memory, the way the game's loader (0x41e176 and 0x41e1a4) calls it. The
// registration itself needs the running game, so after the instructions MinHook moves it
// returns at once; the Mod's hook still runs around it. The pack files are only read.
#pragma once
#include <cstdint>
#include <fstream>
#include <iterator>
#include <vector>
#include "../src/script_patch/script_pack.h"
#include "mapped_exe.h"

namespace script_pack_probe {

using namespace mapped_exe;

constexpr uintptr_t registerScripts = 0x41dfd0;
constexpr uintptr_t afterMovedInstructions = 0x41dfd6;  // push ebp; mov ebp,esp; sub esp,0x20
constexpr uintptr_t scriptPackLoader = 0x41e176;       // eax = texts, [esi+0x30] = the pack
constexpr uintptr_t userCommandPackLoader = 0x41e1a4;  // eax = texts, [esi+0x50] = the pack
constexpr uint32_t scriptPackSlot = 0x30;
constexpr uint32_t userCommandPackSlot = 0x50;

inline std::vector<uint8_t> readFile(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), {});
}

// mov esp,ebp; pop ebp; ret 0xc
inline void stubRegistration() {
    const uint8_t stub[] = {0x8b, 0xe5, 0x5d, 0xc2, 0x0c, 0x00};
    std::memcpy(reinterpret_cast<void*>(va(afterMovedInstructions)), stub, sizeof(stub));
}

// Enters the loader's code right before its call of 0x41dfd0, inside a frame like the
// loader's, so the call returns where the game's does and the loader's epilogue returns here.
#pragma warning(suppress : 4731)
inline void registerThroughLoader(uintptr_t loader, uint32_t packSlot, uint8_t* pack) {
    uint8_t loaderState[0x60] = {};
    uint8_t texts[8] = {};
    *reinterpret_cast<uint8_t**>(loaderState + packSlot) = pack;
    const uintptr_t entry = va(loader);
    uint8_t* state = loaderState;
    uint8_t* textPack = texts;
    __asm {
        mov ecx, entry
        mov edx, state
        mov eax, textPack
        push 0  // Popped by the loader's ret 4
        call enterLoader
        jmp entered
    enterLoader:
        push ebp
        mov ebp, esp
        push esi
        mov esi, edx
        jmp ecx
    entered:
    }
}

inline const int16_t* targetsOf(uint8_t* pack, int32_t script, uint32_t function, uint32_t instruction) {
    using namespace tweaks::script_patch;
    uint8_t* data = findScript(pack, script);
    if (!data) return nullptr;
    return argumentWords(data, instructionAt(data, function, instruction));
}

// The number of int16 words that differ. Every target the tweaks change differs in its low
// byte, so a changed byte starts a word and the byte after it is the same word.
inline size_t changedWordCount(const std::vector<uint8_t>& pack, const std::vector<uint8_t>& original) {
    size_t changed = 0;
    for (size_t offset = 0; offset < pack.size(); ++offset) {
        if (pack[offset] == original[offset]) continue;
        ++changed;
        ++offset;  // The other byte of the same word
    }
    return changed;
}

}  // namespace script_pack_probe
