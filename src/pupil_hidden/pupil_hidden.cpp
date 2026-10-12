// No placement in town belongs to pupils. A townsperson of a pupil's definition turns into the
// pupil as it appears: 0x5ac7d0 (called only from 0x43aa23) returns the pupil's record 40% of
// the time in town, always at the dojo (area 8) and always while challengers are in. Besides,
// while challengers are in, entering an area makes 0x5ac1d0 send one pupil to bring the news.
// The first hook keeps every townsperson outside the dojo as they are; the second makes the
// messenger's area check say "no messenger here", so none is requested at all.
#include "pupil_hidden.h"
#include "../game_code.h"

namespace tweaks {

namespace {

// __stdcall (person definition ID): the pupil's record, or 0 to appear as an ordinary person.
constexpr uintptr_t findPupilToAppear = 0x5ac7d0;
constexpr uint32_t findPupilToAppearHookLength = 5;  // push ebp; mov ebp,esp; mov edx,[0x9ecbb4]
constexpr uintptr_t findPupilToAppearCaller = 0x43aa23;
// call 0x509980 in 0x5ac1d0: true for the areas where no messenger comes (eax = the area).
constexpr uintptr_t messengerAreaCheck = 0x5ac2fb;
constexpr uintptr_t areasWithoutMessenger = 0x509980;
constexpr uint32_t callLength = 5;
constexpr uintptr_t gameStateGlobal = 0x9e967c;
constexpr uintptr_t nightVisitManagerGlobal = 0x9ecbb4;
constexpr uint32_t currentAreaOffset = 0x48b9a;  // uint8 in the game state, as 0x5ac7d0 reads it
constexpr uint8_t dojoArea = 8;

using FindPupilToAppear = void*(__stdcall*)(int definition);

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Pupils Stay in the Dojo");
FindPupilToAppear originalFindPupilToAppear;
void* originalMessengerAreaCheck;
uintptr_t afterMessengerAreaCheck;  // Relocated at install

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    const uint8_t prologue[] = {0x55, 0x8b, 0xec, 0x8b, 0x15};  // push ebp; mov ebp,esp; mov edx,[0x9ecbb4]
    const uint8_t prologueEnd[] = {0x83, 0xec, 0x10};
    // mov al,[ebx+0x48b9a]; cmp al,8; je; cmp byte [ebp-1],0; jne; cmp al,8; je (area 8 = the dojo)
    const uint8_t dojoCheck[] = {0x8a, 0x83, 0x9a, 0x8b, 0x04, 0x00, 0x3c, 0x08, 0x74, 0x1e, 0x80, 0x7d, 0xff, 0x00,
        0x75, 0x18, 0x3c, 0x08, 0x74, 0x14};
    // cmp byte [esi+0x1407c],0; je end; mov eax,edi; call 0x509980; test al,al; jne end
    const uint8_t messengerBefore[] = {0x80, 0xbe, 0x7c, 0x40, 0x01, 0x00, 0x00, 0x0f, 0x84, 0xaa, 0x00, 0x00, 0x00,
        0x8b, 0xc7};
    const uint8_t messengerAfter[] = {0x84, 0xc0, 0x0f, 0x85, 0x9b, 0x00, 0x00, 0x00};
    return game.codeIs(findPupilToAppear, prologue, sizeof(prologue)) &&
           game.operandIs(findPupilToAppear + 5, nightVisitManagerGlobal) &&
           game.codeIs(findPupilToAppear + 9, prologueEnd, sizeof(prologueEnd)) &&
           game.callsTo(findPupilToAppearCaller, findPupilToAppear) &&
           game.codeIs(0x5ac8c2, dojoCheck, sizeof(dojoCheck)) &&
           game.codeIs(0x5ac2ec, messengerBefore, sizeof(messengerBefore)) &&
           game.callsTo(messengerAreaCheck, areasWithoutMessenger) &&
           game.codeIs(messengerAreaCheck + callLength, messengerAfter, sizeof(messengerAfter));
}

bool isOutsideDojo() {
    const auto* gameState = reinterpret_cast<const uint8_t*>(game.readUint32(gameStateGlobal));
    return gameState && gameState[currentAreaOffset] != dojoArea;
}

// Runs once per townsperson who appears; reads one byte before answering.
void* __stdcall findPupilToAppearHook(int definition) {
    if (enabled == toggleOn && isOutsideDojo()) return nullptr;
    return originalFindPupilToAppear(definition);
}

enum InlineAsmConstant : uint32_t { enabledValue = toggleOn };  // Inline asm takes enums, not constexpr

// Runs once per area change while challengers are in. Only al and the flags change, as the call would.
__declspec(naked) void messengerAreaCheckHook() {
    __asm {
        cmp dword ptr [enabled], enabledValue
        jne askGame
        mov al, 1
        jmp dword ptr [afterMessengerAreaCheck]
    askGame:
        jmp dword ptr [originalMessengerAreaCheck]
    }
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the placement of pupils is not the known version", findPupilToAppear);
        return false;
    }
    afterMessengerAreaCheck = game.address(messengerAreaCheck + callLength);
    if (CodeChangeError error = hookGameCode(context, game, findPupilToAppear, findPupilToAppearHookLength,
            reinterpret_cast<void*>(&findPupilToAppearHook), reinterpret_cast<void**>(&originalFindPupilToAppear))) {
        logger.installFailed(error, findPupilToAppear);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, messengerAreaCheck, callLength,
            reinterpret_cast<void*>(&messengerAreaCheckHook), &originalMessengerAreaCheck)) {
        unhookGameCode(game, findPupilToAppear);
        logger.installFailed(error, messengerAreaCheck);
        return false;
    }
    return true;
}

// 弟子 is "pupils" in the game's messages.
const Option options[] = {
    toggle(L"enabled", {L"弟子がマップに出ない", L"Pupils Stay in the Dojo"}, &enabled, false),
};

}  // namespace

const Tweak pupilHiddenTweak{L"pupil_hidden", options, 1, install};

}  // namespace tweaks
