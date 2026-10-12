// Changes to the game's code that tweaks share: MinHook hooks and their claims, and a log that
// names the tweak. Addresses are the exe's preferred ones; GameImage relocates them.
#pragma once
#include <windows.h>
#include <cstdint>
#include <initializer_list>
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

// A few bytes of the game's code that a tweak switches between the game's own and its own.
// Options change while the game runs, so a patch is written and written back from applied.
class CodePatch {
public:
    static constexpr uint32_t maxLength = 16;

    CodePatch(uintptr_t preferred, std::initializer_list<uint8_t> patchedBytes);
    uintptr_t preferredAddress() const { return preferred; }
    // For bytes known only at run time, such as a relocated address. Call before claim.
    void setPatchedBytes(const uint8_t* bytes);
    // Claims the bytes and keeps the game's own to write back. Check them before (codeMatches).
    CodeChangeError claim(const wots4::ModContext& context, const GameImage& game);
    // Writes the tweak's bytes or the game's own. False if the code could not be made writable.
    bool write(bool patched) const;

private:
    uintptr_t preferred;
    uint32_t length;
    uint8_t* code = nullptr;  // Relocated, set by claim
    uint8_t original[maxLength] = {};
    uint8_t patched[maxLength] = {};
};

// Claims every patch; on failure logs it and returns false (claims made stay, as above).
bool claimPatches(const wots4::ModContext& context, const GameImage& game, CodePatch* patches, size_t count,
    const TweakLog& log);
// Writes every patch, or writes the game's bytes back. Logs a failure once per call.
void writePatches(const CodePatch* patches, size_t count, bool patched, const TweakLog& log);

}  // namespace tweaks
