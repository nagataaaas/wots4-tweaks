// Changes to the game's code that tweaks share: MinHook hooks and their claims, and a log that
// names the tweak. Addresses are the exe's preferred ones; GameImage relocates them.
#pragma once
#include <windows.h>
#include <cstdint>
#include <wots4/mod_api.h>
#include "game_image.h"

namespace tweaks {

// Writes lines to mod_loader.log, starting with the tweak's name as players see it in English.
class TweakLog {
public:
    explicit TweakLog(const char* tweakTitle) : title(tweakTitle) {}
    void attach(const wots4::ModContext& context);
    // "[ERROR] <title>: <reason> at 0x<address>; the tweak is skipped."
    void installFailed(const char* reason, uintptr_t preferredAddress) const;
    void write(const char* level, const char* message) const;

private:
    const char* title;
    const wots4::HostApi* host = nullptr;
    HMODULE self = nullptr;
};

// nullptr on success, or why a change to the game's code failed (for TweakLog::installFailed).
using CodeChangeError = const char*;

// Hooks the instruction(s) at preferred with MinHook and claims claimLength bytes there. On
// failure nothing is hooked; a claim that was made stays, since claims cannot be released one by one.
CodeChangeError hookGameCode(const wots4::ModContext& context, const GameImage& game, uintptr_t preferred,
    uint32_t claimLength, void* detour, void** original);
// Removes a hook made by hookGameCode, to undo a tweak whose later step failed.
void unhookGameCode(const GameImage& game, uintptr_t preferred);

}  // namespace tweaks
