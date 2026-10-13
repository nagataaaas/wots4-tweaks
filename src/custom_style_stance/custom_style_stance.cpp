// A My Style slot holds the moves of one stance: its move numbers index that stance's
// SkillComboData. This tweak shows the cards of the slot's other stances of the same weapon group
// on the move select screen (stance_menu.h, stance_charts.h) and puts the chosen ones into the
// style under numbers the slot's stance does not use (stance_rules.h). Their motions and records
// follow them (mixed_style.h).
#include "custom_style_stance.h"
#include "../game_code.h"
#include "game_calls.h"
#include "mixed_style.h"
#include "stance_menu.h"
#include "stance_rules.h"

namespace tweaks {

namespace {

using namespace custom_style_stance;

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Mixed-Stance My Style");
bool installed = false;

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    setUpGameCalls(game);
    // Works whether the option is on or off: a style made with it keeps its moves playable.
    if (!installMixedStyle(context, game, logger)) return false;
    if (!installStanceRules(context, game, logger)) {
        uninstallMixedStyle(game);
        return false;
    }
    if (!installStanceMenu(context, game, logger)) {
        uninstallStanceRules(game);
        uninstallMixedStyle(game);
        return false;
    }
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (!installed) return;
    setStanceRulesEnabled(value == toggleOn, logger);
    setStanceMenuEnabled(value == toggleOn, logger);
}

// 俺流 is "My Style" and 構え is "Stance" in the game's menus.
const Option options[] = {
    toggle(L"enabled", {L"俺流構え混合", L"Mixed-Stance My Style"}, &enabled, false, applied),
};

}  // namespace

const Tweak customStyleStanceTweak{L"custom_style_stance", options, 1, install};

}  // namespace tweaks
