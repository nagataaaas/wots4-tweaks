#pragma once
#include <vector>
#include "tweak.h"

namespace tweaks {

// Every tweak of this Mod, in the order their options appear on the options pages.
std::vector<const Tweak*> registeredTweaks();

}  // namespace tweaks
