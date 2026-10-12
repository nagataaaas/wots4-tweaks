// To add a tweak, put it in src/<tweak name>/, declare it in that folder's header,
// add the source to CMakeLists.txt and list it below. For example:
//
//   static uint32_t enabled;
//   static const tweaks::Option options[] = {
//       tweaks::toggle(L"enabled", L"Label shown in the menu", &enabled, false),
//   };
//   const tweaks::Tweak exampleTweak{L"example", options, 1, installExample};
#include "tweak_list.h"
#include "laura_yobai/laura_yobai.h"

namespace tweaks {

std::vector<const Tweak*> registeredTweaks() {
    return {&lauraYobaiTweak};
}

}  // namespace tweaks
