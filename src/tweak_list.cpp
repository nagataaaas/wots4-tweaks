// To add a tweak, put it in src/<tweak name>/, declare it in that folder's header,
// add the source to CMakeLists.txt and list it in a group below. For example:
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
#include "custom_style_stance/custom_style_stance.h"
#include "instant_job/instant_job.h"
#include "persuade_success/persuade_success.h"
#include "smith_best_roll/smith_best_roll.h"
#include "refine_quality_limit/refine_quality_limit.h"
#include "setup_anytime/setup_anytime.h"
#include "recruit_success/recruit_success.h"
#include "pupil_survival/pupil_survival.h"
#include "pupil_hidden/pupil_hidden.h"
#include "dojo_raid_defeat/dojo_raid_defeat.h"
#include "dojo_raid_appearance/dojo_raid_appearance.h"
#include "dojo_raid_lineup/dojo_raid_lineup.h"
#include "custom_style_all_arts/custom_style_all_arts.h"
#include "accessory_all/accessory_all.h"
#include "rensatsu_gauge/rensatsu_gauge.h"
#include "unlock_all/unlock_all.h"
#include "appearance_all/appearance_all.h"
#include "big_shot_marks/big_shot_marks.h"

namespace tweaks {

std::vector<TweakGroup> registeredTweakGroups() {
    // GameTextUI 87 夜這い / Night Crawling, 12 鍛冶屋 / Smithy, 245 その他 / Other. The game's
    // 道場 (13) is "Back Roads" in English, so the dojo header has its own texts.
    return {
        {wots4::localizedGameText(87), {&lauraYobaiTweak, &persuadeSuccessTweak}},
        {{L"俺流", L"My Style"}, {&customStyleLimitTweak, &customStyleStanceTweak, &customStyleAllArtsTweak}},
        {wots4::localizedGameText(12), {&smithBestRollTweak, &refineQualityLimitTweak}},
        {{L"道場", L"Dojo"}, {&recruitSuccessTweak, &pupilSurvivalTweak, &pupilHiddenTweak, &dojoRaidDefeatTweak,
            &dojoRaidAppearanceTweak, &dojoRaidLineupTweak}},
        {wots4::localizedGameText(245), {&instantJobTweak, &lootDropTweak, &setupAnytimeTweak, &rensatsuGaugeTweak,
            &unlockAllTweak, &appearanceAllTweak, &accessoryAllTweak, &bigShotMarksTweak}},
    };
}

}  // namespace tweaks
