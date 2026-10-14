// Collects the options of the tweaks, shows them on one options page under the headers of
// their groups (the ModLoader scrolls it past wots4::optionsItemsPerScreen rows), and keeps
// their values in config.ini.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "tweak.h"

namespace tweaks {

// Writes one line to mod_loader.log.
using LogFunction = void (*)(const char* message);

class OptionsMenu {
public:
    OptionsMenu(std::wstring configPath, LogFunction log);
    // The ModLoader keeps a pointer to the menu, so it never moves.
    OptionsMenu(const OptionsMenu&) = delete;
    OptionsMenu& operator=(const OptionsMenu&) = delete;

    // False, with the reason in mod_loader.log, if the tweak's definition breaks the
    // limits of the options pages. Check this before installing the tweak.
    bool isWellFormed(const Tweak& tweak) const;
    // Starts a group: header goes above the options added next. A group without
    // options shows no header.
    void beginGroup(const Text& header);
    // Adds the options of a well-formed tweak, sets each value from config.ini
    // (the default if missing or invalid) and calls its applied callback.
    void add(const Tweak& tweak);
    // Adds the options page to the ModLoader. Call once, during Wots4ModInitialize,
    // after every add. False if there are no options or the ModLoader refused the page.
    bool addPage(const wots4::ModContext& context);
    // Writes every value to config.ini if the player changed any since the last save.
    void saveIfChanged();

    size_t optionCount() const { return addedOptionCount; }

private:
    // One row of the page: an option, or a header (option is null).
    struct Entry {
        const Tweak* tweak;
        const Option* option;
        Text header;
    };

    static void WINAPI readPage(void* menu, uint32_t* values, uint32_t count);
    static void WINAPI changeOption(void* menu, uint32_t item, uint32_t value);

    bool tryAddPage(const wots4::ModContext& context);
    // False if there were none.
    bool removeHeaders();
    uint32_t readSavedValue(const Tweak& tweak, const Option& option) const;
    bool writeValues();
    void logFormatted(const char* format, ...) const;

    std::wstring configPath;
    LogFunction log;
    std::vector<Entry> entries;
    size_t addedOptionCount = 0;
    Text pendingHeader{};  // Of the current group until its first option; null texts when none
    bool pageAdded = false;
    bool changedSinceSave = false;
};

}  // namespace tweaks
