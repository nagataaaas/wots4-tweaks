#include "options_menu.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace tweaks {

namespace {

// "Tweaks" is the Mod's name; in Japanese it reads oddly beside the game's 〜設定 pages (user decision 2026-10-14).
constexpr wots4::LocalizedText pageTitle{L"便利機能", L"Tweaks"};
constexpr uint32_t toggleChoiceCount = 2;
// Long enough for any uint32_t in decimal. A longer value in config.ini is invalid anyway.
constexpr size_t maxValueChars = 15;

// Why the option breaks the limits of the options pages, or nullptr if it does not.
const char* findDefinitionError(const Option& option) {
    if (!option.key || !*option.key) return "has no config.ini key";
    if (!option.label.japanese || !option.label.english) return "has no label in every language";
    if (!option.value) return "has no value to write to";
    if (!option.choices) return "has no choices";
    if (option.kind != wots4::optionToggle && option.kind != wots4::optionChoice) return "has an unknown kind";
    if (option.kind == wots4::optionToggle && option.choiceCount != toggleChoiceCount)
        return "is a toggle without exactly 2 choices";
    if (option.choiceCount < 2 || option.choiceCount > wots4::maxOptionChoices) return "does not have 2 to 10 choices";
    if (option.defaultValue >= option.choiceCount) return "has a default outside its choices";
    for (uint32_t index = 0; index < option.choiceCount; ++index)
        if (!option.choices[index].japanese || !option.choices[index].english)
            return "has a choice without a text in every language";
    return nullptr;
}

}  // namespace

OptionsMenu::OptionsMenu(std::wstring configPath, LogFunction log) : configPath(std::move(configPath)), log(log) {}

bool OptionsMenu::isWellFormed(const Tweak& tweak) const {
    if (!tweak.name || !*tweak.name) {
        logFormatted("[ERROR] A tweak without a name is skipped.");
        return false;
    }
    if (tweak.optionCount > 0 && !tweak.options) {
        logFormatted("[ERROR] Tweak %ls is skipped: it has %u options but no option list.", tweak.name, tweak.optionCount);
        return false;
    }
    for (uint32_t index = 0; index < tweak.optionCount; ++index) {
        const char* error = findDefinitionError(tweak.options[index]);
        if (!error) continue;
        logFormatted("[ERROR] Tweak %ls is skipped: option %u %s.", tweak.name, index, error);
        return false;
    }
    return true;
}

void OptionsMenu::beginGroup(const Text& header) {
    pendingHeader = header;
}

void OptionsMenu::add(const Tweak& tweak) {
    for (uint32_t index = 0; index < tweak.optionCount; ++index) {
        const Option& option = tweak.options[index];
        *option.value = readSavedValue(tweak, option);
        if (option.applied) option.applied(*option.value);
        if (pendingHeader.japanese) {
            entries.push_back({nullptr, nullptr, pendingHeader});
            pendingHeader = {};
        }
        entries.push_back({&tweak, &option, {}});
        ++addedOptionCount;
    }
}

uint32_t OptionsMenu::readSavedValue(const Tweak& tweak, const Option& option) const {
    wchar_t text[maxValueChars + 1];
    const DWORD length = GetPrivateProfileStringW(tweak.name, option.key, L"", text, _countof(text), configPath.c_str());
    if (length == 0) return option.defaultValue;

    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (*end == L'\0' && value < option.choiceCount) return static_cast<uint32_t>(value);
    logFormatted("[WARN] config.ini [%ls] %ls=%ls is not a number from 0 to %u; using the default %u.",
        tweak.name, option.key, text, option.choiceCount - 1, option.defaultValue);
    return option.defaultValue;
}

bool OptionsMenu::addPage(const wots4::ModContext& context) {
    if (pageAdded || addedOptionCount == 0) return false;
    pageAdded = tryAddPage(context);
    if (!pageAdded && removeHeaders()) {
        logFormatted("[WARN] The ModLoader did not accept the options page with headers; "
                     "adding it without them (a ModLoader without headers refuses them).");
        pageAdded = tryAddPage(context);
    }
    if (!pageAdded)
        logFormatted("[ERROR] The ModLoader did not accept the options page (%u options); "
                     "those options keep their config.ini values but cannot be changed in the game.",
            static_cast<uint32_t>(addedOptionCount));
    return pageAdded;
}

bool OptionsMenu::tryAddPage(const wots4::ModContext& context) {
    std::vector<wots4::OptionItem> items;
    items.reserve(entries.size());
    for (const Entry& entry : entries) {
        if (!entry.option) {
            items.push_back({wots4::optionHeader, entry.header, nullptr, 0, 0});
            continue;
        }
        const Option& option = *entry.option;
        items.push_back({option.kind, option.label, option.choices, option.choiceCount, option.defaultValue});
    }
    const wots4::OptionsPageCallbacks callbacks{this, readPage, changeOption};
    const auto itemCount = static_cast<uint32_t>(items.size());
    return context.host->addOptionsPage(context.module, &pageTitle, items.data(), itemCount, &callbacks) != FALSE;
}

bool OptionsMenu::removeHeaders() {
    const auto isHeader = [](const Entry& entry) { return !entry.option; };
    const auto firstHeader = std::remove_if(entries.begin(), entries.end(), isHeader);
    if (firstHeader == entries.end()) return false;
    entries.erase(firstHeader, entries.end());
    return true;
}

void WINAPI OptionsMenu::readPage(void* context, uint32_t* values, uint32_t count) {
    const OptionsMenu& menu = *static_cast<const OptionsMenu*>(context);
    if (count != menu.entries.size()) return;
    for (uint32_t item = 0; item < count; ++item) {
        const Option* option = menu.entries[item].option;
        values[item] = option ? *option->value : 0;  // The ModLoader does not use a header's value
    }
}

void WINAPI OptionsMenu::changeOption(void* context, uint32_t item, uint32_t value) {
    OptionsMenu& menu = *static_cast<OptionsMenu*>(context);
    if (item >= menu.entries.size() || !menu.entries[item].option) return;
    const Option& option = *menu.entries[item].option;
    if (value >= option.choiceCount) return;

    *option.value = value;
    if (option.applied) option.applied(value);
    menu.changedSinceSave = true;
}

void OptionsMenu::saveIfChanged() {
    if (!changedSinceSave) return;
    // Left set on failure, so the next save of the game's options tries again.
    if (writeValues()) changedSinceSave = false;
}

bool OptionsMenu::writeValues() {
    for (const Entry& entry : entries) {
        if (!entry.option) continue;
        wchar_t text[maxValueChars + 1];
        swprintf_s(text, L"%u", *entry.option->value);
        if (WritePrivateProfileStringW(entry.tweak->name, entry.option->key, text, configPath.c_str())) continue;
        logFormatted("[ERROR] Could not write [%ls] %ls to config.ini (Windows error %lu); "
                     "the changed options apply until the game exits but are not saved.",
            entry.tweak->name, entry.option->key, GetLastError());
        return false;
    }
    // Flushes the cache Windows keeps for profile files.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, configPath.c_str());
    return true;
}

void OptionsMenu::logFormatted(const char* format, ...) const {
    if (!log) return;
    char line[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf_s(line, _TRUNCATE, format, arguments);
    va_end(arguments);
    log(line);
}

}  // namespace tweaks
