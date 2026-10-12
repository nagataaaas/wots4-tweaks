// To add a tweak, put it in src/<tweak name>/, declare it in that folder's header,
// add the source to CMakeLists.txt and list it below. For example:
//
//   static uint32_t enabled;
//   static const tweaks::Option options[] = {
//       tweaks::toggle(L"enabled", {L"日本語の項目名", L"English Label"}, &enabled, false),
//   };
//   const tweaks::Tweak exampleTweak{L"example", options, 1, installExample};
#include "tweak_list.h"
#include "laura_yobai/laura_yobai.h"
#include "loot_drop/loot_drop.h"
#include "custom_style_limit/custom_style_limit.h"
#include "instant_job/instant_job.h"
#include "persuade_success/persuade_success.h"

namespace tweaks {

std::vector<const Tweak*> registeredTweaks() {
    return {&lauraYobaiTweak, &lootDropTweak, &customStyleLimitTweak, &instantJobTweak, &persuadeSuccessTweak};
}

}  // namespace tweaks
