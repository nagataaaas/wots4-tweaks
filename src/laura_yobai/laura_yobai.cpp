// Sets the Laura mark again at the start of every night visit, from the promise in the
// game state, before the game builds the partners from it. See laura_mark.h.
#include "laura_yobai.h"
#include <cstdio>
#include "../game_image.h"
#include "../minhook_setup.h"
#include "laura_mark.h"

namespace tweaks {

namespace {

using namespace laura_yobai;

// Builds the partners of a night visit (the person in the futon and the one who replaces
// them) from the promise and the Laura mark. edi = the night visit progress [0x9ecbb8].
// Called once per visit, only from 0x4eb45a; every other read of the mark comes after it.
constexpr uintptr_t createPartners = 0x69f280;
constexpr uint32_t createPartnersHookLength = 5;  // mov eax,[0x9e967c]
constexpr uintptr_t createPartnersCaller = 0x4eb454;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uintptr_t yobaiManagerGlobal = 0x9ecbb4;
constexpr uintptr_t yobaiProgressGlobal = 0x9ecbb8;
constexpr uintptr_t definitionRootGlobal = 0x9e9640;
constexpr uintptr_t findDefinition = 0x43c250;  // eax = table holder, ecx = ID

uint32_t partnerSetting = partnerAsTheGameDecides;
GameImage game;
const wots4::HostApi* host;
HMODULE self;
void* originalCreatePartners;

void log(const char* message) {
    if (host && host->log) host->log(self, message);
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov eax,[0x9e967c]; push esi; mov esi,[eax+0x14054]
    const uint8_t prologue[] = {0x56, 0x8b, 0xb0, 0x54, 0x40, 0x01, 0x00};
    // mov edi,[0x9ecbb8]; call 0x69f280
    const uint8_t caller[] = {0x8b, 0x3d};
    // mov eax,[0x9e9640]; mov eax,[eax]; cmp dword [eax],4; jne; mov eax,[eax+0x24]
    const uint8_t definitionRoot[] = {0x8b, 0x00, 0x83, 0x38, 0x04, 0x75, 0x05, 0x8b, 0x40, 0x24};
    const uint8_t lookup[] = {0x8b, 0x00, 0x85, 0xc0, 0x74, 0x0f, 0x39, 0x08, 0x7e, 0x0b, 0x8b, 0x4c,
        0x88, 0x0c, 0x85, 0xc9, 0x7e, 0x03, 0x03, 0xc1, 0xc3};
    // cmp word [reg+6],0x13; mov edx,[ebp+8]; sete cl; mov [edx+0xa8],cl (both places that make a promise)
    const uint8_t promiseByCommand[] = {0x66, 0x83, 0x78, 0x06, 0x13, 0x8b, 0x55, 0x08, 0x0f, 0x94, 0xc1,
        0x88, 0x8a, 0xa8, 0x00, 0x00, 0x00};
    const uint8_t promiseByScript[] = {0x66, 0x83, 0x7e, 0x06, 0x13, 0x8b, 0x55, 0x08, 0x0f, 0x94, 0xc1,
        0x88, 0x8a, 0xa8, 0x00, 0x00, 0x00};
    // mov eax,[0x9ecbb4]; cmp byte [eax+0xa8],0
    const uint8_t readMark[] = {0x80, 0xb8, 0xa8, 0x00, 0x00, 0x00, 0x00};
    const uint8_t moveFromGlobal = 0xa1;
    return game.codeIs(createPartners, &moveFromGlobal, 1) && game.operandIs(createPartners + 1, gameStateGlobal) &&
           game.codeIs(createPartners + 5, prologue, sizeof(prologue)) &&
           game.codeIs(createPartnersCaller, caller, sizeof(caller)) &&
           game.operandIs(createPartnersCaller + 2, yobaiProgressGlobal) &&
           game.callsTo(createPartnersCaller + 6, createPartners) &&
           game.codeIs(0x6a028a, &moveFromGlobal, 1) && game.operandIs(0x6a028b, definitionRootGlobal) &&
           game.codeIs(0x6a028f, definitionRoot, sizeof(definitionRoot)) && game.callsTo(0x6a02a9, findDefinition) &&
           game.codeIs(findDefinition, lookup, sizeof(lookup)) &&
           game.codeIs(0x69aa71, promiseByCommand, sizeof(promiseByCommand)) &&
           game.codeIs(0x69abda, promiseByScript, sizeof(promiseByScript)) &&
           game.codeIs(0x4eb815, &moveFromGlobal, 1) && game.operandIs(0x4eb816, yobaiManagerGlobal) &&
           game.codeIs(0x4eb81a, readMark, sizeof(readMark));
}

// mov eax,[0x9e9640]; mov eax,[eax] (0x6a028a): the global points to the root's pointer.
const uint8_t* definitionRoot() {
    const auto* rootPointer = reinterpret_cast<const uint8_t* const*>(game.readUint32(definitionRootGlobal));
    return rootPointer ? *rootPointer : nullptr;
}

const char* settingName(uint32_t setting) {
    return setting == partnerFake ? "fake (sailor)" : "real (Laura)";
}

// Runs once per night visit, so it may log.
void __cdecl updateMarkBeforePartnersAreCreated() {
    if (!fixesPartner(partnerSetting)) return;
    const MarkUpdate update = updateLauraMark(partnerSetting,
        reinterpret_cast<uint8_t*>(game.readUint32(yobaiManagerGlobal)),
        reinterpret_cast<const uint8_t*>(game.readUint32(gameStateGlobal)), definitionRoot());
    if (!update.written || update.before == update.after) return;
    char line[200];
    sprintf_s(line, "[INFO] Night visit with person definition %d: Laura mark changed from %u to %u "
                    "(partner fixed to %s).",
        update.promisedDefinition, update.before, update.after, settingName(partnerSetting));
    log(line);
}

// 0x69f280 takes the progress in edi, so the registers are kept around the C++ call.
__declspec(naked) void createPartnersHook() {
    __asm {
        pushad
        call updateMarkBeforePartnersAreCreated
        popad
        jmp dword ptr [originalCreatePartners]
    }
}

void logInstallError(const char* reason) {
    char line[200];
    sprintf_s(line, "[ERROR] Laura night visit partner: %s.", reason);
    log(line);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    host = context.host;
    self = context.module;
    if (!codeMatches()) {
        logInstallError("the game code at 0x69f280 is not the known version");
        return false;
    }
    if (!startMinHook()) {
        logInstallError("MinHook failed to initialize");
        return false;
    }
    auto* target = reinterpret_cast<void*>(game.address(createPartners));
    if (MH_CreateHook(target, reinterpret_cast<void*>(&createPartnersHook), &originalCreatePartners) != MH_OK) {
        logInstallError("MinHook could not create the hook at 0x69f280");
        return false;
    }
    if (!host->claim(self, game.address(createPartners), createPartnersHookLength)) {
        MH_RemoveHook(target);
        logInstallError("0x69f280 is used by another Mod");
        return false;
    }
    // The claim cannot be released alone (releaseClaims drops every tweak's claims), so it is
    // kept if enabling fails; no hook is left in the range then.
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        logInstallError("MinHook could not enable the hook at 0x69f280");
        return false;
    }
    return true;
}

// Shown on the options page, so in Japanese like the game's menus.
const wchar_t* const partnerChoices[] = {L"固定しない", L"偽物", L"本物"};
const Option options[] = {
    choice(L"partner", L"ローラの夜這い相手を固定する", partnerChoices, &partnerSetting, partnerAsTheGameDecides),
};

}  // namespace

const Tweak lauraYobaiTweak{L"laura_yobai", options, 1, install};

}  // namespace tweaks
