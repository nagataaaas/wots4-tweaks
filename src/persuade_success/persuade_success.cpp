// Wooing is decided in the action scripts, not in the exe:
// - Townsfolk (TemplateMob, function 14 when talked to): four questions add up a score, and
//   instruction 406 (command 36) jumps to the refusal at 423 when it is under 100.
// - Named characters attacked with Nightly Pleasure Martial Arts (function 5 of their scripts): one of
//   six command 35 rolls by the style's level, then a second command 35 roll, must both land
//   on the branch that makes the promise.
// The branches below jump to the promise instead. Whoever cannot be wooed (people with
// attribute 0x2, or with a promise already made tonight) is turned down before these branches.
#include "persuade_success.h"
#include "../script_patch/script_patch.h"

namespace tweaks {

namespace {

using script_patch::ScriptBranch;
using script_patch::compareJumpCommand;
using script_patch::randomJumpCommand;

constexpr int32_t templateMobScript = 1;
constexpr uint32_t templateMobFunctions = 15;
constexpr uint32_t talkedToFunction = 14;
constexpr uint32_t talkedToInstructions = 881;
constexpr uint32_t namedFunctions = 15;
constexpr uint32_t attackedFunction = 5;

// A named character's wooing: six first rolls (one per level of the style, from
// firstRolls), then the second roll. A miss goes to missTarget in both.
struct NamedWooing {
    int32_t script;
    uint32_t attackedInstructions;
    uint32_t firstRolls;
    uint32_t secondRoll;
    int16_t missTarget;
    int16_t firstRollHit;
    int16_t promise;
};

constexpr NamedWooing named[] = {
    {10, 122, 45, 51, 52, 51, 77},    // Akagi_01
    {11, 123, 47, 53, 54, 53, 79},    // Kogure_01 (second roll 1 in 5)
    {13, 128, 77, 83, 84, 83, 109},   // Kotobuki_01
    {14, 108, 57, 63, 64, 63, 89},    // Moro_01
    {18, 106, 55, 61, 62, 61, 87},    // Kinugawa_01
    {21, 116, 39, 45, 46, 45, 71},    // Jenkins_01
    {65, 88, 18, 25, 26, 24, 53},     // Dojima (first rolls go to 24, a command before the second roll at 25)
};
constexpr uint32_t firstRollsPerCharacter = 6;
constexpr size_t branchCount = 1 + std::size(named) * (firstRollsPerCharacter + 1);

struct BranchTable {
    ScriptBranch branches[branchCount];
};

constexpr BranchTable makeBranches() {
    BranchTable table{};
    size_t next = 0;
    // TemplateMob 406: "score < 100 -> 423 (turned down)" falls through to 407 (the promise)
    table.branches[next++] = {templateMobScript, templateMobFunctions, talkedToFunction, talkedToInstructions, 406,
        compareJumpCommand, 423, 407};
    for (const NamedWooing& character : named) {
        for (uint32_t level = 0; level < firstRollsPerCharacter; ++level)
            table.branches[next++] = {character.script, namedFunctions, attackedFunction,
                character.attackedInstructions, character.firstRolls + level, randomJumpCommand, character.missTarget,
                character.firstRollHit};
        table.branches[next++] = {character.script, namedFunctions, attackedFunction, character.attackedInstructions,
            character.secondRoll, randomJumpCommand, character.missTarget, character.promise};
    }
    return table;
}
constexpr BranchTable branchTable = makeBranches();

uint32_t enabled = toggleOff;
TweakLog logger("Guaranteed Wooing");
script_patch::BranchSet branchSet(branchTable.branches, branchCount, &enabled, &logger);

bool install(const wots4::ModContext& context) {
    logger.attach(context);
    return script_patch::install(context, branchSet, logger);
}

void applied(uint32_t value) {
    branchSet.write(value == toggleOn);
}

// The game's English says "woo" for 口説く.
const Option options[] = {
    toggle(L"enabled", {L"口説き確定成功", L"Guaranteed Wooing"}, &enabled, false, applied),
};

}  // namespace

const Tweak persuadeSuccessTweak{L"persuade_success", options, 1, install};

}  // namespace tweaks
