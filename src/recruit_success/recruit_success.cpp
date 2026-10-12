// Recruiting is decided in the action script TemplateMob (function 14, when talked to): after
// 「弟子にしてやる」, instruction 195 (command 35) picks one of {197, 210, 210, 210, 210}, or 196
// picks one of {197, 210} for someone the player knocked down. 197 adds the pupil, 210 is the
// refusal. Both now always go to 197. A closed dojo, no dojo and a full dojo (20 pupils) are
// turned down before these branches, as in the game.
#include "recruit_success.h"
#include "../script_patch/script_patch.h"

namespace tweaks {

namespace {

using script_patch::ScriptBranch;

constexpr int32_t templateMobScript = 1;
constexpr uint32_t templateMobFunctions = 15;
constexpr uint32_t talkedToFunction = 14;
constexpr uint32_t talkedToInstructions = 881;
constexpr int16_t refusal = 210;
constexpr int16_t newPupil = 197;

constexpr ScriptBranch branches[] = {
    {templateMobScript, templateMobFunctions, talkedToFunction, talkedToInstructions, 195,
        script_patch::randomJumpCommand, refusal, newPupil},
    {templateMobScript, templateMobFunctions, talkedToFunction, talkedToInstructions, 196,
        script_patch::randomJumpCommand, refusal, newPupil},
};

uint32_t enabled = toggleOff;
TweakLog logger("Guaranteed Pupils");
script_patch::BranchSet branchSet(branches, std::size(branches), &enabled, &logger);

bool install(const wots4::ModContext& context) {
    logger.attach(context);
    return script_patch::install(context, branchSet, logger);
}

void applied(uint32_t value) {
    branchSet.write(value == toggleOn);
}

// 弟子 is "pupils" in the game's messages (「道場破りは弟子が撃退した！」).
const Option options[] = {
    toggle(L"enabled", {L"弟子確定成功", L"Guaranteed Pupils"}, &enabled, false, applied),
};

}  // namespace

const Tweak recruitSuccessTweak{L"recruit_success", options, 1, install};

}  // namespace tweaks
