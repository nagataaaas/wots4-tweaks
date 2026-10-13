# tweaks

[日本語](README.md)

> [!NOTE]
> This Mod is for the [Steam version of Way of the Samurai 4](https://store.steampowered.com/app/312780/4/?l=english) only.

A collection of small "nice to have" tweaks. Each tweak can be changed individually on the "Tweaks" page added to the game's options.

## Features

- **Laura's Night Crawling**: choose who waits in the futon when you have a night crawling appointment with Laura.
  - Don't Fix: unchanged from the game. Who you get depends on things like whether you restarted the game after making the appointment.
  - Fake: when you lift the futon, it turns out to be someone else disguised as Laura.
  - Real: Laura herself is waiting.
- **Guaranteed Loot**: searching or kicking a body always gives an item on the first try, as long as the body has something to give. The odds of which item you get are unchanged. Items that townsfolk drop on their own when they die are not affected.
- **Unlimited My Style**: when creating a My Style, you can add as many moves as you like, even without enough Ability Points. The "total / Ability Points" display is unchanged, so the total may appear to exceed your points. No Ability Points are spent.
- **Instant Missions**: a mission succeeds as soon as you accept it and the conversation ends. You get the same rewards, town order and records as for a normal success, except for bonus rewards (such as those for the number of enemies defeated). Night missions accepted during the day also succeed right away.
  - This includes the mission chains of Nodobotoke, Kobato, the Thief, Gramps and Shirube, battle missions such as Underbelly Recon and Government Recon, and the final mission, The Amihama Eight. Clients offer their next request just as they would after a normal success.
  - The fights and conversations with the targets are skipped. Accepting Part 1 of Grandpa's Memories or A Lid On Something Stinky also accepts Part 2, and both parts succeed.
- **Guaranteed Wooing**: wooing always ends in a nighttime promise, no matter how you answer. Wooing a named character of Nightly Pleasure Martial Arts (Akagi, Kogure, Kotobuki, Moro, Kinugawa, J.J. or Dojima) also succeeds on the first try, whatever your level in the style. People who cannot be wooed still turn you down, as does everyone once you already have a partner for tonight.
- **Best Smithing**: when you ask the Smithy to Refine or Recast a weapon to make it stronger, its attack always rises by the maximum amount (Dojima's extra bonus is maxed too). Making a weapon harder or polishing it already gives fixed amounts, so those are unchanged.
- **Unlimited Refining**: the Smithy refines a weapon (to make it stronger or harder) even when its degradation has reached the weapon's maximum. From degradation 21, making it stronger raises attack by degradation + 1. The caps of 500 attack, 2000 maximum durability and 999 degradation stay.
- **Guaranteed Pupils**: asking someone to become your pupil always works. You are still turned down if you have no dojo, if the dojo is closed, or if it is full (20 pupils).
- **Pupils Survive Challengers**: while challengers are at the dojo, talking to the pupil who brings the news leads to an ordinary conversation instead of a pupil dying or the dojo falling, so no pupil dies because of it. Pupils who fall in the fight at the dojo, or whom you cut down in town, still die as usual.
- **Pupils Stay in the Dojo**: your pupils no longer go out into town and stay in the dojo instead. When challengers come, no pupil comes into town to tell you. The number of pupils and the dojo's reputation are unaffected.
- **Repelling Challengers**: choose whether the sensei and your pupils repel dojo challengers (decided each time you move to another area).
  - Default: unchanged from the game.
  - Always: the sensei or a pupil always repels them.
  - Never: neither the sensei nor your pupils repel them.
- **Challenger Appearance**: choose whether dojo challengers come (decided each time you move to another area).
  - Default: unchanged from the game.
  - Always: challengers come whenever they can. As in the game, they never come at night, when you are leaving the dojo, when you have no pupils, or when the dojo's reputation is at its lowest.
  - Never: challengers never come.

## Installation

> [!WARNING]
> Install [wots4-mods (ModLoader)](https://github.com/nagataaaas/wots4-mods) first.

1. Close the game.
2. Download `wots4-mod-tweaks-<version>.zip` from [Releases](../../releases) and extract it.
3. Copy the extracted `mods` folder into the game's installation folder (the folder containing `WayOfTheSamurai4.exe`). If a `mods` folder already exists, merge the contents into it.
4. Make sure `{game installation folder}/mods/tweaks/mod.dll` exists, then start the game.

## Settings

Change the tweaks on the "Tweaks" page, which is added to the options on the title screen and to 各種設定 in the pause menu. With more than 8 options, the page scrolls up and down. The options are shown in the game's language (Japanese or English).

Your settings are saved to `{game installation folder}/mods/tweaks/config.ini` when the game saves its options, and carry over the next time you start the game.
