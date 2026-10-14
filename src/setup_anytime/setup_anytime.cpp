// After Load (途中から) on the title screen, the game opens the difficulty selection and then the
// character creation only when the save's phase (save 0x0010, word [data+0x28]) is 0, which is
// the state the end of a playthrough leaves. Skipping that test opens them for any save; once the
// character is saved, the game goes on from the save's phase as Load does.
//
// The difficulty selection uses the select window the title made when it opened ([title+0x4c]).
// Opening Options on the title creates a new select window in the UI manager (0x6763df) and
// deletes the title's, so [title+0x4c] is left dangling; the hook points it at the current one.
#include "setup_anytime.h"
#include "../game_code.h"

namespace tweaks {

namespace {

constexpr uintptr_t phaseZeroJump = 0x67ce07;  // jne 0x67ce0d (phase is not 0) -> nop, nop
// The title's difficulty selection, a state of GamePartTitle: ecx = the title, [esp+4] = the
// state event (-1 when it opens).
constexpr uintptr_t difficultySelection = 0x4ed530;
constexpr uint32_t difficultySelectionHookLength = 6;  // push ebp; mov ebp,esp; and esp,-8
constexpr int32_t stateOpens = -1;
constexpr uint32_t titleSelectWindowOffset = 0x4c;
constexpr uint32_t titleUiManagerOffset = 0x58;
constexpr uint32_t uiManagerSelectWindowOffset = 4 + 0x1b * 4;  // UI type 0x1b in [manager+4+type*4]

CodePatch patches[] = {
    CodePatch(phaseZeroJump, {0x90, 0x90}),
};

uint32_t enabled = toggleOff;
GameImage game;
TweakLog logger("Setup Anytime");
bool installed = false;

using DifficultySelectionFunction = void(__fastcall*)(uint8_t* title, void* unusedEdx, int32_t event);
DifficultySelectionFunction originalDifficultySelection;

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches() {
    // mov al,[ebx+0x44] (title menu cursor); add al,2; mov [ebx+0x3c],al (what the title does);
    // cmp al,2 (Load); jne 0x67ce0d; mov eax,[ebx+0x1c4] (game data); cmp word [eax+0x28],0 (phase);
    // jne 0x67ce0d; mov byte [ebx+0x3c],1 (difficulty selection)
    const uint8_t loadPhaseCheck[] = {0x8a, 0x43, 0x44, 0x04, 0x02, 0x88, 0x43, 0x3c, 0x3c, 0x02, 0x75, 0x11, 0x8b,
        0x83, 0xc4, 0x01, 0x00, 0x00, 0x66, 0x83, 0x78, 0x28, 0x00, 0x75, 0x04, 0xc6, 0x43, 0x3c, 0x01};
    // push ebp; mov ebp,esp; and esp,-8; push -1
    const uint8_t selectionPrologue[] = {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x6a, 0xff};
    // mov eax,[ebp+8] (event); cmp eax,-1; je (open the window)
    const uint8_t selectionEvent[] = {0x8b, 0x45, 0x08, 0x83, 0xf8, 0xff, 0x0f, 0x84};
    // The title opening: mov edx,[edi+0x58] (UI manager); push edx; mov esi,0x1b; call 0x67fcf0
    // (create UI); mov [edi+0x4c],eax
    const uint8_t titleCreatesSelectWindow[] = {0x8b, 0x57, 0x58, 0x52, 0xbe, 0x1b, 0x00, 0x00, 0x00};
    const uint8_t storesSelectWindow[] = {0x89, 0x47, 0x4c};
    // The UI manager's lookup: mov eax,[edi+esi*4+4]; ret
    const uint8_t uiLookup[] = {0x8b, 0x44, 0xb7, 0x04, 0xc3};
    return game.codeIs(0x67cdf0, loadPhaseCheck, sizeof(loadPhaseCheck)) &&
           game.codeIs(difficultySelection, selectionPrologue, sizeof(selectionPrologue)) &&
           game.codeIs(0x4ed55e, selectionEvent, sizeof(selectionEvent)) &&
           game.codeIs(0x4ed242, titleCreatesSelectWindow, sizeof(titleCreatesSelectWindow)) &&
           game.callsTo(0x4ed24b, 0x67fcf0) && game.codeIs(0x4ed250, storesSelectWindow, sizeof(storesSelectWindow)) &&
           game.codeIs(0x67fcdc, uiLookup, sizeof(uiLookup));
}

void* fieldAt(uint8_t* object, uint32_t offset) {
    return *reinterpret_cast<void**>(object + offset);
}

// Runs once each time the difficulty selection opens, so it may log.
void useCurrentSelectWindow(uint8_t* title) {
    auto* uiManager = static_cast<uint8_t*>(fieldAt(title, titleUiManagerOffset));
    if (!uiManager) return;
    void* current = fieldAt(uiManager, uiManagerSelectWindowOffset);
    if (!current || current == fieldAt(title, titleSelectWindowOffset)) return;
    *reinterpret_cast<void**>(title + titleSelectWindowOffset) = current;
    logger.write("INFO", "the title's select window had been replaced (by Options); the difficulty selection uses the "
                         "current one");
}

// Fixes the pointer whatever the option is: the game's own phase 0 Load reaches the same window.
void __fastcall difficultySelectionHook(uint8_t* title, void* unusedEdx, int32_t event) {
    if (event == stateOpens) useCurrentSelectWindow(title);
    originalDifficultySelection(title, unusedEdx, event);
}

bool install(const wots4::ModContext& context) {
    game = GameImage(context.game);
    logger.attach(context);
    if (!codeMatches()) {
        logger.installFailed("the title screen's Load or difficulty selection is not the known version", 0x67cdf0);
        return false;
    }
    if (CodeChangeError error = hookGameCode(context, game, difficultySelection, difficultySelectionHookLength,
            reinterpret_cast<void*>(&difficultySelectionHook), reinterpret_cast<void**>(&originalDifficultySelection))) {
        logger.installFailed(error, difficultySelection);
        return false;
    }
    if (!claimPatches(context, game, patches, _countof(patches), logger)) {
        unhookGameCode(game, difficultySelection);
        return false;
    }
    installed = true;
    return true;
}

void applied(uint32_t value) {
    if (installed) writePatches(patches, _countof(patches), value == toggleOn, logger);
}

const Option options[] = {
    toggle(L"enabled", {L"いつでも初期設定", L"Setup Anytime"}, &enabled, false, applied),
};

}  // namespace

const Tweak setupAnytimeTweak{L"setup_anytime", options, 1, install};

}  // namespace tweaks
