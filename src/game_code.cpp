#include "game_code.h"
#include <cstdio>
#include <cstring>
#include "minhook_setup.h"

namespace tweaks {

void TweakLog::attach(const wots4::ModContext& context) {
    host = context.host;
    self = context.module;
}

void TweakLog::installFailed(const char* reason, uintptr_t preferredAddress) const {
    char line[256];
    sprintf_s(line, "%s at 0x%x; the tweak is skipped", reason, static_cast<unsigned>(preferredAddress));
    write("ERROR", line);
}

void TweakLog::write(const char* level, const char* message) const {
    if (!host || !host->log) return;
    char line[320];
    sprintf_s(line, "[%s] %s: %s.", level, title, message);
    host->log(self, line);
}

CodeChangeError hookGameCode(const wots4::ModContext& context, const GameImage& game, uintptr_t preferred,
    uint32_t claimLength, void* detour, void** original) {
    if (!startMinHook()) return "MinHook failed to initialize";
    auto* target = reinterpret_cast<void*>(game.address(preferred));
    if (MH_CreateHook(target, detour, original) != MH_OK) return "MinHook could not create the hook";
    if (!context.host->claim(context.module, game.address(preferred), claimLength)) {
        MH_RemoveHook(target);
        return "the code is used by another Mod";
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        return "MinHook could not enable the hook";
    }
    return nullptr;
}

void unhookGameCode(const GameImage& game, uintptr_t preferred) {
    MH_RemoveHook(reinterpret_cast<void*>(game.address(preferred)));
}

CodePatch::CodePatch(uintptr_t preferred, std::initializer_list<uint8_t> patchedBytes)
    : preferred(preferred), length(static_cast<uint32_t>(patchedBytes.size())) {
    std::memcpy(patched, patchedBytes.begin(), length < maxLength ? length : maxLength);
}

void CodePatch::setPatchedBytes(const uint8_t* bytes) {
    std::memcpy(patched, bytes, length);
}

CodeChangeError CodePatch::claim(const wots4::ModContext& context, const GameImage& game) {
    if (length == 0 || length > maxLength) return "the patch has no bytes or too many";
    if (!context.host->claim(context.module, game.address(preferred), length)) return "the code is used by another Mod";
    code = reinterpret_cast<uint8_t*>(game.address(preferred));
    std::memcpy(original, code, length);
    return nullptr;
}

bool CodePatch::write(bool usePatched) const {
    if (!code) return false;
    DWORD oldProtection;
    if (!VirtualProtect(code, length, PAGE_EXECUTE_READWRITE, &oldProtection)) return false;
    std::memcpy(code, usePatched ? patched : original, length);
    VirtualProtect(code, length, oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), code, length);
    return true;
}

RandFunction gameRand(const GameImage& game) {
    return reinterpret_cast<RandFunction>(game.readUint32(randImport));
}

bool readsRand(const GameImage& game, uintptr_t preferred, uint8_t opcode, uint8_t modRm) {
    const uint8_t instruction[] = {opcode, modRm};
    return game.codeIs(preferred, instruction, sizeof(instruction)) && game.operandIs(preferred + 2, randImport);
}

void readRandFrom(CodePatch& patch, const GameImage& game, const RandFunction* slot) {
    uint8_t bytes[randReadLength];
    std::memcpy(bytes, reinterpret_cast<const void*>(game.address(patch.preferredAddress())), 2);
    const auto slotAddress = reinterpret_cast<uint32_t>(slot);
    std::memcpy(bytes + 2, &slotAddress, sizeof(slotAddress));
    patch.setPatchedBytes(bytes);
}

bool claimPatches(const wots4::ModContext& context, const GameImage& game, CodePatch* patches, size_t count,
    const TweakLog& log) {
    for (size_t index = 0; index < count; ++index) {
        if (CodeChangeError error = patches[index].claim(context, game)) {
            log.installFailed(error, patches[index].preferredAddress());
            return false;
        }
    }
    return true;
}

void writePatches(const CodePatch* patches, size_t count, bool patched, const TweakLog& log) {
    for (size_t index = 0; index < count; ++index) {
        if (patches[index].write(patched)) continue;
        char line[160];
        sprintf_s(line, "could not make the code at 0x%x writable; the option has no effect until the game restarts",
            static_cast<unsigned>(patches[index].preferredAddress()));
        log.write("ERROR", line);
        return;
    }
}

}  // namespace tweaks
