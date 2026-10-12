// Changes where branches of the action scripts jump, for tweaks that make a scripted roll
// always succeed. The pack is loaded after the Mod initializes, so one hook on the game's
// registration of the scripts (0x41dfd0) finds the branches of every tweak once it is loaded.
#pragma once
#include <cstdint>
#include <vector>
#include <wots4/mod_api.h>
#include "../game_code.h"
#include "../tweak.h"
#include "script_pack.h"

namespace tweaks::script_patch {

// A command 35 or 36 in a function of a script. Every jump target of it that is missTarget
// becomes successTarget; the others must be successTarget already. The counts identify the
// version of the script; a script that does not match is left as it is.
struct ScriptBranch {
    int32_t script;
    uint32_t functionCount;
    uint32_t function;
    uint32_t functionInstructionCount;
    uint32_t instruction;
    uint16_t command;
    int16_t missTarget;
    int16_t successTarget;
};

// The branches of one tweak, and the argument words found for them in the loaded pack.
class BranchSet {
public:
    BranchSet(const ScriptBranch* branches, size_t count, const uint32_t* enabled, const TweakLog* log)
        : branches(branches), count(count), enabled(enabled), log(log) {}

    // Finds the words in the pack, then writes them for the current option.
    void locate(uint8_t* pack);
    // Writes the success targets, or the game's own targets back. Does nothing before the pack loads.
    void write(bool success) const;
    size_t locatedWordCount() const { return words.size(); }

private:
    struct Word {
        int16_t* address;
        int16_t missTarget;
        int16_t successTarget;
    };
    bool collectScriptWords(uint8_t* pack, int32_t script, std::vector<Word>& found) const;
    bool collectBranchWords(uint8_t* script, const ScriptBranch& branch, std::vector<Word>& found) const;

    const ScriptBranch* branches;
    size_t count;
    const uint32_t* enabled;
    const TweakLog* log;
    std::vector<Word> words;
};

// Hooks 0x41dfd0 on the first call, then adds the set. On failure logs it and returns false.
bool install(const wots4::ModContext& context, BranchSet& set, const TweakLog& log);

}  // namespace tweaks::script_patch
