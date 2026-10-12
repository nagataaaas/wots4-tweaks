// What one tweak declares. A tweak lives in src/<tweak name>/ and is listed in tweak_list.cpp.
// The options menu shows the options of every tweak on the Tweaks options pages, 8 per page,
// in the order of that list, and keeps their values in config.ini under [<tweak name>].
#pragma once
#include <windows.h>
#include <cstdint>
#include <wots4/mod_api.h>

namespace tweaks {

using AppliedCallback = void (*)(uint32_t value);

// One row on an options page. Make it with toggle() or choice().
struct Option {
    wots4::OptionKind kind;
    const wchar_t* key;    // Key in config.ini. ASCII
    const wchar_t* label;  // A string or wots4::gameText(n)
    const wchar_t* const* choices;
    uint32_t choiceCount;
    uint32_t defaultValue;
    // Owned by the tweak. Its hooks read this directly, so checking an option costs one memory read.
    uint32_t* value;
    // Optional. Called on the game thread after *value was set: once after config.ini
    // was read, then each time the player changes the option.
    AppliedCallback applied;
};

struct Tweak {
    const wchar_t* name;  // Section in config.ini and the name in mod_loader.log. ASCII
    const Option* options;
    uint32_t optionCount;
    // Optional. Installs the tweak's hooks during initialization. On failure, undo everything
    // it did and return false: the tweak is skipped and its options are not shown.
    bool (*install)(const wots4::ModContext& context);
};

// GameTextUI 211 "有り" and 212 "無し", shown in the game's language.
inline const wchar_t* const onOffChoices[] = {wots4::gameText(211), wots4::gameText(212)};
constexpr uint32_t toggleOn = 1;
constexpr uint32_t toggleOff = 0;

// An on / off option. *value is toggleOn or toggleOff.
inline Option toggle(const wchar_t* key, const wchar_t* label, uint32_t* value, bool onByDefault,
    AppliedCallback applied = nullptr) {
    return {wots4::optionToggle, key, label, onOffChoices, 2, onByDefault ? toggleOn : toggleOff, value, applied};
}

// An option with 2 to 10 choices. *value is the index of the choice.
template <uint32_t choiceCount>
Option choice(const wchar_t* key, const wchar_t* label, const wchar_t* const (&choices)[choiceCount],
    uint32_t* value, uint32_t defaultChoice, AppliedCallback applied = nullptr) {
    static_assert(choiceCount >= 2 && choiceCount <= wots4::maxOptionChoices, "a choice option has 2 to 10 choices");
    return {wots4::optionChoice, key, label, choices, choiceCount, defaultChoice, value, applied};
}

}  // namespace tweaks
