#include "game_code.h"
#include <cstdio>
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

}  // namespace tweaks
