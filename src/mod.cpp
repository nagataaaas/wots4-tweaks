// tweaks: small optional changes to the game. Each tweak lives in src/<tweak name>/, and the
// player switches it on the Tweaks options pages. See tweak.h and tweak_list.cpp.
#include <windows.h>
#include <cstdio>
#include <wots4/mod_api.h>
#include "options_menu.h"
#include "tweak_list.h"

namespace {

const wots4::HostApi* host;
HMODULE self;
// Lives until the process exits: the ModLoader keeps pointers into it.
tweaks::OptionsMenu* optionsMenu;

void hostLog(const char* message) {
    if (host && host->log) host->log(self, message);
}

bool isSupportedContext(const wots4::ModContext* context) {
    if (!context || context->size != sizeof(*context) || context->version != wots4::modApiVersion) return false;
    if (!context->directory || !context->host) return false;
    const wots4::HostApi& api = *context->host;
    return api.size == sizeof(wots4::HostApi) && api.version == wots4::modApiVersion && api.addOptionsPage &&
           api.onOptionsSaved;
}

void WINAPI saveOptions(void*) {
    optionsMenu->saveIfChanged();
}

// Installs every well-formed tweak of the group and adds its options to the menu under the
// group's header. Returns how many were installed.
uint32_t installTweaks(const wots4::ModContext& context, const tweaks::TweakGroup& group) {
    optionsMenu->beginGroup(group.header);
    uint32_t installedCount = 0;
    for (const tweaks::Tweak* tweak : group.tweaks) {
        if (!optionsMenu->isWellFormed(*tweak)) continue;
        if (tweak->install && !tweak->install(context)) {
            char line[256];
            sprintf_s(line, "[ERROR] Tweak %ls could not be installed; it is skipped and its options are not shown.",
                tweak->name);
            hostLog(line);
            continue;
        }
        optionsMenu->add(*tweak);
        ++installedCount;
    }
    return installedCount;
}

}  // namespace

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if (!isSupportedContext(context)) return FALSE;
    host = context->host;
    self = context->module;
    optionsMenu = new tweaks::OptionsMenu(std::wstring(context->directory) + L"config.ini", hostLog);

    uint32_t tweakCount = 0;
    uint32_t installedCount = 0;
    for (const tweaks::TweakGroup& group : tweaks::registeredTweakGroups()) {
        tweakCount += static_cast<uint32_t>(group.tweaks.size());
        installedCount += installTweaks(*context, group);
    }
    bool pageAdded = false;
    if (optionsMenu->optionCount() > 0) {
        pageAdded = optionsMenu->addPage(*context);
        if (!host->onOptionsSaved(self, saveOptions, nullptr))
            hostLog("[ERROR] Could not watch the game's options save; option changes apply until the game exits "
                    "but are not saved to config.ini.");
    }

    char line[256];
    sprintf_s(line, "[INFO] Tweaks initialized: %u of %u tweaks installed, %u options, options page %s.",
        installedCount, tweakCount, static_cast<uint32_t>(optionsMenu->optionCount()),
        pageAdded ? "added" : "not added");
    hostLog(line);
    return TRUE;
}
