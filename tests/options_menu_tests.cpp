// Runs mod.cpp and options_menu.cpp against a fake ModLoader and the tweaks of test_tweak_list.cpp.
// Writes config.ini only in a folder under %TEMP%, which it removes at the end.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <wots4/mod_api.h>
#include "../src/options_menu.h"
#include "test_tweak_list.h"

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context);

namespace {

int passed, failed;
void check(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++passed : ++failed;
}

// ---------------------------------------------------------------- fake ModLoader

struct RecordedPage {
    std::wstring title;  // English
    std::vector<wots4::OptionItem> items;
    wots4::OptionsPageCallbacks callbacks;
    std::wstring japaneseTitle;
};
std::vector<RecordedPage> recordedPages;
std::vector<std::string> loggedLines;
wots4::GameEventCallback optionsSavedCallback;
void* optionsSavedContext;
bool refusePages;
bool refuseHeaders;  // Like a ModLoader that does not know headers or notices

void WINAPI fakeLog(HMODULE, const char* message) {
    loggedLines.emplace_back(message);
}
BOOL WINAPI fakeClaim(HMODULE, uintptr_t, uint32_t) { return TRUE; }
void WINAPI fakeReleaseClaims(HMODULE) {}
BOOL WINAPI fakeAddOptionsPage(HMODULE, const wots4::LocalizedText* title, const wots4::OptionItem* items,
    uint32_t count, const wots4::OptionsPageCallbacks* callbacks) {
    if (refusePages) return FALSE;
    for (uint32_t index = 0; index < count && refuseHeaders; ++index)
        if (items[index].kind == wots4::optionHeader || items[index].kind == wots4::optionNotice) return FALSE;
    recordedPages.push_back(
        {title->english, std::vector<wots4::OptionItem>(items, items + count), *callbacks, title->japanese});
    return TRUE;
}
BOOL WINAPI fakeOnFrame(HMODULE, wots4::GameEventCallback, void*) { return TRUE; }
BOOL WINAPI fakeOnOptionsSaved(HMODULE, wots4::GameEventCallback callback, void* context) {
    optionsSavedCallback = callback;
    optionsSavedContext = context;
    return TRUE;
}

const wots4::HostApi fakeHost{sizeof(wots4::HostApi), wots4::modApiVersion, fakeLog, fakeClaim, fakeReleaseClaims,
    fakeAddOptionsPage, fakeOnFrame, fakeOnOptionsSaved};

void logToFakeHost(const char* message) {
    fakeLog(nullptr, message);
}

bool anyLogContains(const char* text) {
    for (const std::string& line : loggedLines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------- config.ini in %TEMP%

std::wstring testDirectory;
std::wstring configPath;

bool createTestDirectory() {
    wchar_t temporary[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temporary)) return false;
    testDirectory = std::wstring(temporary) + L"wots4_tweaks_tests_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    configPath = testDirectory + L"config.ini";
    return CreateDirectoryW(testDirectory.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
void removeTestDirectory() {
    DeleteFileW(configPath.c_str());
    RemoveDirectoryW(testDirectory.c_str());
}
void writeSetting(const wchar_t* section, const wchar_t* key, const wchar_t* value) {
    WritePrivateProfileStringW(section, key, value, configPath.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, configPath.c_str());
}
UINT readSetting(const wchar_t* section, const wchar_t* key) {
    return GetPrivateProfileIntW(section, key, 999, configPath.c_str());
}
bool configExists() {
    return GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ---------------------------------------------------------------- tests

void testModInitialize() {
    using namespace testing;
    writeSetting(L"alpha", L"first", L"0");     // Default on, saved off
    writeSetting(L"alpha", L"second", L"abc");  // Not a number: default
    writeSetting(L"beta", L"first", L"2");
    writeSetting(L"beta", L"second", L"5");     // Outside the choices: default

    const wots4::ModContext context{sizeof(wots4::ModContext), wots4::modApiVersion, nullptr, nullptr, nullptr,
        testDirectory.c_str(), &fakeHost};
    check(ModInitialize(&context) == TRUE, "the Mod accepts a v2 context");

    check(alphaValues[0] == tweaks::toggleOff, "a saved value replaces the default");
    check(alphaValues[1] == tweaks::toggleOn, "a value that is not a number falls back to the default");
    check(anyLogContains("[alpha] second=abc is not a number from 0 to 1"), "the invalid value is logged");
    check(alphaValues[2] == tweaks::toggleOff, "a missing value is the default");
    check(betaValues[0] == 2 && betaValues[1] == 1, "choice values are read, and an outside value falls back");
    check(betaAppliedCount == betaOptionCount, "applied is called once per option after reading config.ini");
    check(!malformedInstallCalled, "a malformed tweak is not installed");
    check(anyLogContains("Tweak malformed is skipped: option 0 has a default outside its choices"),
        "the malformed definition is logged");
    check(anyLogContains("Tweak failing could not be installed"), "a failed install is logged");

    // Rows: header A, alpha 1-5, header B, beta 1-4. The failing tweak's group has no header.
    check(recordedPages.size() == 1, "9 options make 1 page (the ModLoader scrolls it)");
    if (recordedPages.size() != 1) return;
    const RecordedPage& page = recordedPages[0];
    check(page.items.size() == 11 && page.title == L"Tweaks",
        "the page holds all 9 options and 2 headers under the Mod's name");
    check(page.items[0].kind == wots4::optionHeader && std::wstring(page.items[0].label.english) == L"Group A" &&
              !page.items[0].choices && page.items[0].choiceCount == 0 && page.items[0].defaultValue == 0,
        "a group starts with a header without choices");
    check(page.items[1].kind == wots4::optionToggle && page.items[1].choiceCount == 2, "a toggle has 2 choices");
    check(page.items[1].defaultValue == tweaks::toggleOn && page.items[3].defaultValue == tweaks::toggleOff,
        "each option's default is passed for the reset button");
    check(page.items[6].kind == wots4::optionHeader && std::wstring(page.items[6].label.english) == L"Group B" &&
              page.items[7].kind == wots4::optionChoice && page.items[7].choiceCount == 3,
        "the failing tweak and its group's header are left out, so group B follows alpha");
    check(std::wstring(page.items[10].label.english) == L"Beta 4", "the last option is the 11th item");
    check(anyLogContains("2 of 4 tweaks installed, 9 options, options page added"), "the summary is logged");

    uint32_t values[11] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    page.callbacks.read(page.callbacks.context, values, 11);
    check(values[0] == 0 && values[1] == 0 && values[2] == 1 && values[6] == 0 && values[7] == 2 && values[8] == 1 &&
              values[10] == betaValues[3],
        "read reports the current values, and 0 for the headers");
    uint32_t untouched[11] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7};
    page.callbacks.read(page.callbacks.context, untouched, 9);
    check(untouched[0] == 7, "read with another item count writes nothing");

    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (!optionsSavedCallback) return;
    DeleteFileW(configPath.c_str());
    page.callbacks.changed(page.callbacks.context, 6, 1);
    optionsSavedCallback(optionsSavedContext);
    check(!configExists(), "config.ini is not written when nothing changed, and a header's change is ignored");

    page.callbacks.changed(page.callbacks.context, 10, 0);
    check(betaValues[3] == 0 && betaLastApplied == 0, "a change of the 11th item sets the value and calls applied");
    page.callbacks.changed(page.callbacks.context, 10, 3);
    check(betaValues[3] == 0, "a value outside the choices is ignored");
    page.callbacks.changed(page.callbacks.context, 3, tweaks::toggleOn);
    optionsSavedCallback(optionsSavedContext);
    check(readSetting(L"beta", L"fourth") == 0 && readSetting(L"alpha", L"third") == 1,
        "the options save writes the changed values");
    check(readSetting(L"alpha", L"first") == 0 && readSetting(L"failing", L"enabled") == 999,
        "every shown option is written, and skipped tweaks are not");
}

uint32_t exactlyEightValues[8];
const tweaks::Text eightLabel{L"項目", L"Item"};
const tweaks::Option exactlyEightOptions[8] = {
    tweaks::toggle(L"a", eightLabel, &exactlyEightValues[0], false),
    tweaks::toggle(L"b", eightLabel, &exactlyEightValues[1], false),
    tweaks::toggle(L"c", eightLabel, &exactlyEightValues[2], false),
    tweaks::toggle(L"d", eightLabel, &exactlyEightValues[3], false),
    tweaks::toggle(L"e", eightLabel, &exactlyEightValues[4], false),
    tweaks::toggle(L"f", eightLabel, &exactlyEightValues[5], false),
    tweaks::toggle(L"g", eightLabel, &exactlyEightValues[6], false),
    tweaks::toggle(L"h", eightLabel, &exactlyEightValues[7], false),
};
const tweaks::Tweak exactlyEight{L"eight", exactlyEightOptions, 8, nullptr};

void testPageBoundaries() {
    const wots4::ModContext context{sizeof(wots4::ModContext), wots4::modApiVersion, nullptr, nullptr, nullptr,
        testDirectory.c_str(), &fakeHost};
    recordedPages.clear();

    tweaks::OptionsMenu empty(configPath, logToFakeHost);
    check(!empty.addPage(context) && recordedPages.empty(), "no options add no page");

    tweaks::OptionsMenu eight(configPath, logToFakeHost);
    eight.add(exactlyEight);
    check(eight.addPage(context) && recordedPages.size() == 1, "8 options make 1 page");
    check(!eight.addPage(context) && recordedPages.size() == 1, "the page is added only once");
    check(!recordedPages.empty() && recordedPages[0].title == L"Tweaks" && recordedPages[0].japaneseTitle == L"便利機能",
        "the page is titled Tweaks in English and 便利機能 in Japanese");
    check(!recordedPages.empty() && recordedPages[0].items[0].label.japanese == eightLabel.japanese &&
              recordedPages[0].items[0].label.english == eightLabel.english,
        "both languages of a label are passed to the ModLoader");

    refusePages = true;
    tweaks::OptionsMenu refused(configPath, logToFakeHost);
    refused.add(exactlyEight);
    check(!refused.addPage(context), "a refused page is reported");
    check(anyLogContains("did not accept the options page (8 options)"), "a refused page is logged");
    refusePages = false;

    refuseHeaders = true;
    recordedPages.clear();
    tweaks::OptionsMenu withoutHeaders(configPath, logToFakeHost);
    withoutHeaders.beginGroup(eightLabel);
    withoutHeaders.add(exactlyEight);
    check(withoutHeaders.addPage(context) && recordedPages.size() == 1 && recordedPages[0].items.size() == 8 &&
              recordedPages[0].items[0].kind == wots4::optionToggle,
        "a ModLoader that refuses headers gets the page without them");
    check(anyLogContains("did not accept the options page with headers"), "the page without headers is logged");
    if (!recordedPages.empty()) {
        const wots4::OptionsPageCallbacks& callbacks = recordedPages[0].callbacks;
        callbacks.changed(callbacks.context, 0, tweaks::toggleOn);
        check(exactlyEightValues[0] == tweaks::toggleOn, "without headers, item 0 is the first option");
        exactlyEightValues[0] = tweaks::toggleOff;
    }
    refuseHeaders = false;
}

uint32_t restartValue;
uint32_t afterRestartValue;
uint32_t restartAppliedCount;
void restartApplied(uint32_t) { ++restartAppliedCount; }
const tweaks::Text restartSizes[] = {{L"小", L"Small"}, {L"中", L"Medium"}, {L"大", L"Large"}};
const tweaks::Text restartNotice{L"再起動後に有効", L"After a restart"};
const tweaks::Option restartOptions[] = {
    tweaks::restartChoice(L"size", {L"大きさ", L"Size"}, restartSizes, &restartValue, 0, restartNotice,
        restartApplied),
    tweaks::toggle(L"after", {L"次", L"After"}, &afterRestartValue, false),
};
const tweaks::Tweak restartTweak{L"restart", restartOptions, 2, nullptr};

void testRestartNotices() {
    const wots4::ModContext context{sizeof(wots4::ModContext), wots4::modApiVersion, nullptr, nullptr, nullptr,
        testDirectory.c_str(), &fakeHost};
    recordedPages.clear();
    DeleteFileW(configPath.c_str());
    writeSetting(L"restart", L"size", L"2");

    tweaks::OptionsMenu menu(configPath, logToFakeHost);
    check(menu.isWellFormed(restartTweak), "a restart choice is well formed");
    menu.beginGroup(testing::inBoth(L"Restart"));
    menu.add(restartTweak);
    check(restartValue == 2 && restartAppliedCount == 1, "a restart choice is read and applied like a choice");
    check(menu.addPage(context) && recordedPages.size() == 1, "the page with a notice is added");
    if (recordedPages.empty()) return;
    const RecordedPage& page = recordedPages[0];
    check(page.items.size() == 4 && page.items[1].kind == wots4::optionChoice &&
              page.items[2].kind == wots4::optionNotice && page.items[3].kind == wots4::optionToggle,
        "rows: header, the restart choice, its notice, the next option");
    check(page.items.size() == 4 && page.items[2].defaultValue == 1 && page.items[2].choiceCount == 0b011 &&
              std::wstring(page.items[2].label.english) == L"After a restart",
        "the notice points at the choice and shows for every value but the startup one (2)");

    uint32_t values[4] = {7, 7, 7, 7};
    page.callbacks.read(page.callbacks.context, values, 4);
    check(values[1] == 2 && values[2] == 0 && values[3] == 0, "read reports the choice, and 0 for the notice");
    page.callbacks.changed(page.callbacks.context, 2, 1);
    menu.saveIfChanged();
    check(readSetting(L"restart", L"size") == 2, "a change of the notice row is ignored");
    page.callbacks.changed(page.callbacks.context, 1, 0);
    check(restartValue == 0 && restartAppliedCount == 2, "a change of the restart choice sets it and calls applied");
    menu.saveIfChanged();
    check(readSetting(L"restart", L"size") == 0 && readSetting(L"restart", L"after") == 0,
        "the save writes the options and no key for the notice");

    refuseHeaders = true;
    recordedPages.clear();
    tweaks::OptionsMenu withoutNotices(configPath, logToFakeHost);
    withoutNotices.beginGroup(testing::inBoth(L"Restart"));
    withoutNotices.add(restartTweak);
    check(withoutNotices.addPage(context) && recordedPages.size() == 1 && recordedPages[0].items.size() == 2,
        "a ModLoader that refuses notices gets the page without them");
    refuseHeaders = false;

    const tweaks::Option halfNotice = tweaks::restartChoice(L"key", {L"項目", L"Label"}, restartSizes, &restartValue,
        0, {L"再起動", nullptr}, nullptr);
    check(!menu.isWellFormed(tweaks::Tweak{L"half", &halfNotice, 1, nullptr}),
        "a restart notice needs the text in every language");
}

void testMalformedDefinitions() {
    uint32_t value = 0;
    const tweaks::Text label{L"項目", L"Label"};
    const tweaks::Text threeChoices[] = {{L"1", L"1"}, {L"2", L"2"}, {L"3", L"3"}};
    const tweaks::Option toggleWithThree{wots4::optionToggle, L"key", label, threeChoices, 3, 0, &value, nullptr};
    const tweaks::Option withoutKey = tweaks::toggle(L"", label, &value, false);
    const tweaks::Option withoutValue = tweaks::toggle(L"key", label, nullptr, false);
    const tweaks::Option withoutEnglishLabel = tweaks::toggle(L"key", {L"項目", nullptr}, &value, false);
    const tweaks::Text choiceWithoutJapanese[] = {{L"1", L"1"}, {nullptr, L"2"}};
    const tweaks::Option withoutJapaneseChoice =
        tweaks::choice(L"key", label, choiceWithoutJapanese, &value, 0);
    const tweaks::Tweak withoutOptionList{L"list", nullptr, 1, nullptr};

    tweaks::OptionsMenu menu(configPath, logToFakeHost);
    check(!menu.isWellFormed(tweaks::Tweak{L"three", &toggleWithThree, 1, nullptr}), "a toggle needs 2 choices");
    check(!menu.isWellFormed(tweaks::Tweak{L"key", &withoutKey, 1, nullptr}), "an option needs a key");
    check(!menu.isWellFormed(tweaks::Tweak{L"value", &withoutValue, 1, nullptr}), "an option needs a value");
    check(!menu.isWellFormed(withoutOptionList), "a tweak with options needs the option list");
    check(!menu.isWellFormed(tweaks::Tweak{L"label", &withoutEnglishLabel, 1, nullptr}),
        "a label needs the English text");
    check(!menu.isWellFormed(tweaks::Tweak{L"choice", &withoutJapaneseChoice, 1, nullptr}),
        "every choice needs the Japanese text");
    check(!menu.isWellFormed(tweaks::Tweak{nullptr, nullptr, 0, nullptr}), "a tweak needs a name");
    check(menu.isWellFormed(tweaks::Tweak{L"none", nullptr, 0, nullptr}), "a tweak may have no options");
}

}  // namespace

int main() {
    if (!createTestDirectory()) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 1;
    }
    testModInitialize();
    testPageBoundaries();
    testRestartNotices();
    testMalformedDefinitions();
    removeTestDirectory();
    std::printf("%d checks passed, %d failed. Wrote config.ini only under %%TEMP%%; "
                "no game installation, saves, or game process were written.\n",
        passed, failed);
    return failed ? 1 : 0;
}
