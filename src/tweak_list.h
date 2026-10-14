#pragma once
#include <vector>
#include "tweak.h"

namespace tweaks {

// Tweaks shown together under a header on the options page.
struct TweakGroup {
    Text header;
    std::vector<const Tweak*> tweaks;
};

// Every tweak of this Mod by group, in the order their options appear on the options page.
std::vector<TweakGroup> registeredTweakGroups();

}  // namespace tweaks
