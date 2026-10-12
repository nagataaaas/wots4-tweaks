#include "script_patch.h"
#include <intrin.h>
#include <cstdio>
#include "script_pack.h"

namespace tweaks::script_patch {

namespace {

// __stdcall (the script map, the pack, the pack of the scripts' texts); ret 0xc. Called once
// for the Script pack (from 0x41e182) and once for the UserCommand pack (from 0x41e1b0),
// each after the pack was read into a heap buffer that lives as long as the game.
constexpr uintptr_t registerScripts = 0x41dfd0;
constexpr uint32_t registerScriptsHookLength = 5;  // push ebp; mov ebp,esp; sub esp,0x20
constexpr uintptr_t scriptPackCall = 0x41e182;
constexpr uintptr_t userCommandPackCall = 0x41e1b0;
constexpr uint32_t callLength = 5;

using RegisterScripts = void(__stdcall*)(void* scriptMap, uint8_t* pack, uint8_t* texts);

GameImage game;
RegisterScripts originalRegisterScripts;
uintptr_t scriptPackReturn;  // Relocated at install
bool hooked = false;
std::vector<BranchSet*> sets;

// The code this relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // push ebp; mov ebp,esp; sub esp,0x20; push ebx; push esi; mov esi,[ebp+0xc] (the pack)
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x20, 0x53, 0x56, 0x8b, 0x75, 0x0c};
    // Reading argument n: [script+4] + [instruction+0xc] + script, then the word at + n*2
    const uint8_t readArgument[] = {0x8b, 0x5b, 0x04, 0x8b, 0x43, 0x04, 0x03, 0x46, 0x0c, 0x03, 0xc3, 0x8b, 0x4d,
        0x0c, 0x0f, 0xb7, 0x04, 0x48};
    // Command 35 counts its targets: the function's instruction count and offset, then stops at
    // the argument count or the first ignored argument (byte +3)
    const uint8_t countTargets[] = {0x0f, 0xb7, 0x44, 0xcb, 0x0c, 0x66, 0x85, 0xc0, 0x74, 0x23, 0x3b, 0xf0, 0x7d,
        0x1f, 0xc1, 0xe6, 0x04, 0x03, 0x74, 0xcb, 0x08, 0x03, 0xf3, 0x74, 0x14, 0x0f, 0xb6, 0x06, 0x8b, 0x5d, 0xfc,
        0x3b, 0xc3, 0x7e, 0x22, 0x8a, 0x4d, 0xf8, 0x84, 0x4e, 0x03};
    return game.codeIs(registerScripts, prologue, sizeof(prologue)) && game.callsTo(scriptPackCall, registerScripts) &&
           game.callsTo(userCommandPackCall, registerScripts) &&
           game.codeIs(0x41e53b, readArgument, sizeof(readArgument)) &&
           game.codeIs(0x410b49, countTargets, sizeof(countTargets));
}

void __stdcall registerScriptsHook(void* scriptMap, uint8_t* pack, uint8_t* texts) {
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    originalRegisterScripts(scriptMap, pack, texts);
    if (caller != scriptPackReturn || !pack) return;
    for (BranchSet* set : sets) set->locate(pack);
}

}  // namespace

void BranchSet::locate(uint8_t* pack) {
    words.clear();
    for (size_t index = 0; index < count; ++index) {
        const int32_t script = branches[index].script;
        bool seenBefore = false;
        for (size_t earlier = 0; earlier < index; ++earlier) seenBefore |= branches[earlier].script == script;
        if (seenBefore) continue;
        std::vector<Word> scriptWords;
        if (collectScriptWords(pack, script, scriptWords)) {
            words.insert(words.end(), scriptWords.begin(), scriptWords.end());
            continue;
        }
        char line[160];
        sprintf_s(line, "action script %d is not the known version; it is left unchanged", script);
        log->write("WARN", line);
    }
    write(*enabled == toggleOn);
}

bool BranchSet::collectScriptWords(uint8_t* pack, int32_t script, std::vector<Word>& found) const {
    uint8_t* scriptData = findScript(pack, script);
    if (!scriptData) return false;
    for (size_t index = 0; index < count; ++index) {
        if (branches[index].script != script) continue;
        if (!collectBranchWords(scriptData, branches[index], found)) return false;
    }
    return true;
}

bool BranchSet::collectBranchWords(uint8_t* script, const ScriptBranch& branch, std::vector<Word>& found) const {
    if (functionCount(script) != branch.functionCount || branch.function >= branch.functionCount) return false;
    if (instructionCount(script, branch.function) != branch.functionInstructionCount ||
        branch.instruction >= branch.functionInstructionCount)
        return false;
    const uint8_t* instruction = instructionAt(script, branch.function, branch.instruction);
    if (commandOf(instruction) != branch.command) return false;
    const uint32_t targetCount = jumpTargetCount(instruction);
    if (targetCount == 0) return false;
    int16_t* targets = argumentWords(script, instruction);
    for (uint32_t index = 0; index < targetCount; ++index) {
        if (targets[index] == branch.missTarget)
            found.push_back({&targets[index], branch.missTarget, branch.successTarget});
        else if (targets[index] != branch.successTarget)
            return false;
    }
    return true;
}

void BranchSet::write(bool success) const {
    for (const Word& word : words) *word.address = success ? word.successTarget : word.missTarget;
}

bool install(const wots4::ModContext& context, BranchSet& set, const TweakLog& log) {
    if (!hooked) {
        game = GameImage(context.game);
        if (!codeMatches()) {
            log.installFailed("the loading of the action scripts is not the known version", registerScripts);
            return false;
        }
        scriptPackReturn = game.address(scriptPackCall + callLength);
        if (CodeChangeError error = hookGameCode(context, game, registerScripts, registerScriptsHookLength,
                reinterpret_cast<void*>(&registerScriptsHook), reinterpret_cast<void**>(&originalRegisterScripts))) {
            log.installFailed(error, registerScripts);
            return false;
        }
        hooked = true;
    }
    sets.push_back(&set);
    return true;
}

}  // namespace tweaks::script_patch
