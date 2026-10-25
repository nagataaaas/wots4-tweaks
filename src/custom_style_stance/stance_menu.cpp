#include "stance_menu.h"
#include <cstring>
#include "game_calls.h"
#include "my_style_screen.h"
#include "stance_charts.h"

namespace tweaks::custom_style_stance {

namespace {

// The screen's layout list (0x979664): +0x8 layout entries, +0xc their count, +0x10 animation
// entries, +0x14 their count. The menu buttons are layouts 1-4; button i (0-3) plays animation
// 1 + 2i ("releace", when chosen) or 2 + 2i ("select", otherwise), as 0x5cb070 and 0x5cb110 do.
constexpr uintptr_t gameLayoutList = 0x979664;
constexpr uint32_t layoutListSize = 0x40;
constexpr uint32_t layoutEntriesOffset = 0x8;
constexpr uint32_t layoutCountOffset = 0xc;
constexpr uint32_t animationEntriesOffset = 0x10;
constexpr uint32_t animationCountOffset = 0x14;
constexpr uint32_t layoutEntrySize = 0x40;
constexpr uint32_t animationEntrySize = 0x6c;
constexpr uint32_t animationLayoutOffset = 0x68;  // int, the layout the animation plays on
constexpr uint32_t gameLayoutCount = 5;
constexpr uint32_t gameAnimationCount = 9;
constexpr uint32_t layoutCount = gameLayoutCount + 1;
constexpr uint32_t animationCount = gameAnimationCount + 2;

constexpr int buttonCount = 5;
constexpr int stanceButton = 4;
constexpr int lastGameButton = 3;
constexpr int moveSelectButton = 0;
// The buttons from left to right: the stance button sits between Move Select and Auto-Select
// (user decision 2026-10-13). Inside, it keeps number 4 so the game's buttons keep theirs.
constexpr int buttonsInOrder[buttonCount] = {moveSelectButton, stanceButton, 1, 2, lastGameButton};
constexpr int stanceButtonLayout = stanceButton + 1;
constexpr int firstButtonAnimation = 1;
constexpr int animationsPerButton = 2;
constexpr int noAnimation = -1;
constexpr uint32_t dialogConfirmKey = 0x8000;  // Opens and closes the confirmation dialog

int buttonAnimation(int button, bool chosen) {
    return firstButtonAnimation + animationsPerButton * button + (chosen ? 0 : 1);
}

// The copy of the layout list, with a sixth layout (the stance button) and its two animations.
uint8_t layoutList[layoutListSize];
uint8_t layoutEntries[layoutCount][layoutEntrySize];
uint8_t animationEntries[animationCount][animationEntrySize];

// The buttons' labels: the game draws label i from screen +0xbc + 0x80 * (i - 1); the fifth
// one is drawn from here instead.
constexpr wchar_t japaneseLabel[] = L"構え切替";
constexpr wchar_t englishLabel[] = L"Switch Stance";  // 構え is "Stance" in the game (UI 140)
constexpr uint32_t stanceTextNumber = 140;
const wchar_t* stanceButtonLabel = englishLabel;
const wots4::HostApi* host = nullptr;
uintptr_t drawLabelReturn;  // Relocated 0x5ca87d

const uint32_t* decideKeys;  // Masks of the game's key settings
const uint32_t* cancelKeys;
const uint32_t* upKeys;
const uint32_t* downKeys;
uintptr_t menuLeaveHolder;  // Relocated 0x9ecf30
constexpr uint32_t menuLeaveFlag = 0x1c01;
uintptr_t gameMenuInput;  // Relocated 0x5cb1a0, the menu's input step (the hook runs from there)

CodePatch patches[] = {
    // lea eax,[ebx+5] -> 6: the screen makes room for six layouts (0x5b9d20)
    CodePatch(0x5ca10c, {0x8d, 0x43, 0x06}),
    // mov dword ptr [esi+8],0x979664 -> the copy of the layout list (set at install)
    CodePatch(0x5ca10f, {0xc7, 0x46, 0x08, 0, 0, 0, 0}),
    // Drawing the labels: cmp eax,4 -> 5
    CodePatch(0x5ca945, {0x83, 0xf8, 0x05}),
    // Drawing the labels: mov esi,[esp+0x10]; test esi,esi -> jmp drawButtonLabel (set at install)
    CodePatch(0x5ca877, {0xe9, 0, 0, 0, 0, 0x90}),
    // Entering the menu: the layouts placed, cmp ebx,0x14 -> 0x18 (layouts 1-5)
    CodePatch(0x5caf10, {0x83, 0xfb, 0x18}),
    // Entering the menu: the buttons highlighted, cmp esi,4 -> 5
    CodePatch(0x5caf34, {0x83, 0xfe, 0x05}),
};
enum PatchIndex { layoutListPatch = 1, drawLabelPatch = 3 };

using PlaceButtons = void(__thiscall*)(void* screen);
using HighlightButton = void(__thiscall*)(void* screen, int button, uint32_t chosen, uint32_t atOnce);
using HighlightCursor = void(__thiscall*)(void* screen);
using MenuInput = void(__thiscall*)(void* screen, uint32_t pressed, uint32_t repeated);
PlaceButtons originalPlaceButtons;
HighlightButton originalHighlightButton;
HighlightCursor originalHighlightCursor;
MenuInput originalMenuInput;
void* originalReleaseShared;

bool hasStanceButton(void* screen) {
    return field<void*>(screen, MoveSelectScreen::layoutList) == layoutList;
}

int& menuCursor(void* screen) {
    return field<int>(screen, MoveSelectScreen::menuCursor);
}

void* layoutOf(void* screen, int index) {
    return field<void**>(screen, MoveSelectScreen::layouts)[index];
}

// Label i is drawn in the loop at 0x5ca85b with i (1-5) at [esp+0x24] and its text at [esp+0x10].
enum DrawLabelConstant : uint32_t { stanceButtonLabelIndex = stanceButtonLayout };

__declspec(naked) void drawButtonLabel() {
    __asm {
        mov esi, dword ptr [esp + 0x10]
        cmp dword ptr [esp + 0x24], stanceButtonLabelIndex
        jne keepLabel
        mov esi, stanceButtonLabel
    keepLabel:
        test esi, esi
        jmp drawLabelReturn
    }
}

// The game's texts are loaded after the Mods start, so the language is looked up when a screen
// is made.
void chooseLabelLanguage() {
    const wchar_t* stance = findUiText(host, stanceTextNumber);  // 構え or Stance
    if (!stance) return;
    constexpr wchar_t firstJapaneseCharacter = 0x3000;
    stanceButtonLabel = stance[0] >= firstJapaneseCharacter ? japaneseLabel : englishLabel;
}

// The game places buttons 1-4 at the "wazaFunc0N_pos" points of the screen's layout, evenly
// along a bar drawn for four. There is no fifth point, so the five buttons are spread over the
// same bar: it spans four steps, half a step out from the first and the last point.
void spreadButtonsOverBar(void* screen) {
    void* layouts[buttonCount];
    for (int button = 0; button < buttonCount; ++button) {
        layouts[button] = layoutOf(screen, button + 1);
        if (!layouts[button]) return;
    }
    const float firstX = field<float>(layouts[0], layoutX);
    const float lastGameX = field<float>(layouts[lastGameButton], layoutX);
    const float gameStep = (lastGameX - firstX) / lastGameButton;
    const float barLeft = firstX - gameStep / 2;
    const float step = gameStep * (lastGameButton + 1) / buttonCount;
    field<float>(layouts[stanceButton], layoutY) = field<float>(layouts[lastGameButton], layoutY);
    for (int place = 0; place < buttonCount; ++place) {
        void* layout = layouts[buttonsInOrder[place]];
        field<float>(layout, layoutX) = barLeft + step * (place + 0.5f);
        placeLayout(layout);
    }
}

void __fastcall placeButtons(void* screen, void*) {
    const bool firstTime = !field<bool>(screen, MoveSelectScreen::layoutsPlaced);
    originalPlaceButtons(screen);
    if (!firstTime || !hasStanceButton(screen)) return;
    chooseLabelLanguage();
    spreadButtonsOverBar(screen);
}

void __fastcall highlightButton(void* screen, void*, int button, uint32_t chosen, uint32_t atOnce) {
    if (button != stanceButton || !hasStanceButton(screen)) {
        originalHighlightButton(screen, button, chosen, atOnce);
        return;
    }
    // The flags are bytes pushed as dwords.
    const int animation = buttonAnimation(button, (chosen & 0xff) != 0);
    if (atOnce & 0xff) {
        playAnimationAtOnce(screen, animation);
    } else {
        playAnimation(screen, animation);
    }
    field<int>(screen, MoveSelectScreen::playingAnimation) = animation;
}

void __fastcall highlightCursor(void* screen, void*) {
    if (!hasStanceButton(screen)) {
        originalHighlightCursor(screen);
        return;
    }
    const int cursor = menuCursor(screen);
    for (int button = 0; button < buttonCount; ++button) {
        const int animation = buttonAnimation(button, button == cursor);
        playAnimation(screen, animation);
        field<int>(screen, MoveSelectScreen::playingAnimation) = animation;
    }
}

// The menu takes input only when no button animation plays and no dialog is open or opening
// (0x5cb1bc-0x5cb23e).
bool menuTakesInput(void* screen, uint32_t pressed) {
    if (field<int>(screen, MoveSelectScreen::playingAnimation) > noAnimation) return false;
    void* dialog = field<void*>(screen, MoveSelectScreen::dialog);
    return !dialog || (!field<bool>(dialog, dialogOpen) && !(pressed & dialogConfirmKey));
}

int placeOf(int button) {
    for (int place = 0; place < buttonCount; ++place) {
        if (buttonsInOrder[place] == button) return place;
    }
    return 0;
}

// [[0x9ecf30]+0x1c01] makes the menu leave as if cancelled (0x5cb4f2).
bool menuForcedToLeave() {
    const auto* holder = *reinterpret_cast<const uint8_t* const*>(menuLeaveHolder);
    return holder && holder[menuLeaveFlag];
}

// The list of stances lists the group's stances in order, the common chart, then Quit.
constexpr uint32_t quitText = 59;  // やめる / Quit
StanceChoice listedStances;

void playStanceButtonAtOnce(void* screen, bool chosen) {
    const int animation = buttonAnimation(stanceButton, chosen);
    playAnimationAtOnce(screen, animation);
    field<int>(screen, MoveSelectScreen::playingAnimation) = animation;
}

// Waits for the list of stances to close, as the game waits for Auto-Select's (0x5cba70), then
// goes back to the menu and shows the chosen stance.
void __fastcall stanceListInput(void* screen, void*, uint32_t, uint32_t) {
    void*& window = field<void*>(screen, MoveSelectScreen::choiceWindow);
    if (window && field<int>(window, ChoiceWindow::state) != choiceWindowClosed) return;
    const int chosen = window ? field<int>(window, ChoiceWindow::chosen) : -1;
    window = nullptr;
    field<uintptr_t>(screen, MoveSelectScreen::inputStep) = gameMenuInput;
    playStanceButtonAtOnce(screen, true);
    if (chosen >= 0 && chosen < listedStances.count) showStance(screen, listedStances.charts[chosen]);
}

void openStanceList(void* screen) {
    StanceChoice choice;
    if (!stanceChoiceOf(screen, choice)) return;
    const wchar_t* choices[largestGroupSize + 2];
    int count = 0;
    for (int place = 0; place < choice.count; ++place) {
        const wchar_t* name = chartName(choice.charts[place]);
        if (!name) return;
        choices[count++] = name;
    }
    const wchar_t* quit = findUiText(host, quitText);
    if (!quit) quit = stanceButtonLabel == japaneseLabel ? L"やめる" : L"Quit";  // A ModLoader too old to find it
    choices[count++] = quit;
    void* window = openTextChoiceWindow(choices, count, choice.shown);
    if (!window) return;
    listedStances = choice;
    field<void*>(screen, MoveSelectScreen::choiceWindow) = window;
    field<uintptr_t>(screen, MoveSelectScreen::inputStep) = reinterpret_cast<uintptr_t>(&stanceListInput);
    // The button is let go while the list is open, as Auto-Select's (0x5cb358).
    playStanceButtonAtOnce(screen, false);
}

// The game's menu (0x5cb1a0) knows four buttons: it moves the cursor by one over 0-3 and does
// nothing when the fifth is decided. The tweak moves the cursor itself in the order on screen,
// highlighting once: highlighting a button twice in one frame (the game's step, then a fix)
// leaves it unhighlighted. Deciding the fifth opens the list of stances.
void __fastcall menuInput(void* screen, void*, uint32_t pressed, uint32_t repeated) {
    if (!hasStanceButton(screen)) {
        originalMenuInput(screen, pressed, repeated);
        return;
    }
    const int before = menuCursor(screen);
    const bool takesInput = menuTakesInput(screen, pressed);
    const bool decided = takesInput && (pressed & *decideKeys);
    const bool cancelled = takesInput && !decided && ((pressed & *cancelKeys) || menuForcedToLeave());
    // The game checks the previous key first (0x5cb507), then the next one.
    const int move = (repeated & *upKeys) ? -1 : (repeated & *downKeys) ? 1 : 0;
    if (takesInput && !decided && !cancelled && move != 0) {
        menuCursor(screen) = buttonsInOrder[(placeOf(before) + move + buttonCount) % buttonCount];
        highlightCursor(screen, nullptr);
        playCursorSound();
        return;
    }
    const bool leavesMenu = (decided && before != moveSelectButton && before != stanceButton) || cancelled;
    if (leavesMenu) showGameChart(screen);
    originalMenuInput(screen, pressed, repeated);  // Plays the decide sound for the fifth too
    if (decided && before == stanceButton) openStanceList(screen);
}

// 0x5c9ea0 frees the shared data [esi+0xc8] and deletes the slots' charts through it.
enum ReleaseSharedConstant : uint32_t { ownerSharedOffset = 0xc8 };

void __cdecl releaseChartsOf(void* shared) {
    releaseCharts(shared);
}

__declspec(naked) void releaseShared() {
    __asm {
        pushad
        push dword ptr [esi + ownerSharedOffset]
        call releaseChartsOf
        add esp, 4
        popad
        jmp originalReleaseShared
    }
}

void copyLayoutList(const GameImage& game) {
    const auto* gameList = reinterpret_cast<const uint8_t*>(game.address(gameLayoutList));
    std::memcpy(layoutList, gameList, layoutListSize);
    const auto* gameLayouts = *reinterpret_cast<const uint8_t* const*>(gameList + layoutEntriesOffset);
    const auto* gameAnimations = *reinterpret_cast<const uint8_t* const*>(gameList + animationEntriesOffset);
    std::memcpy(layoutEntries, gameLayouts, gameLayoutCount * layoutEntrySize);
    std::memcpy(layoutEntries[gameLayoutCount], layoutEntries[gameLayoutCount - 1], layoutEntrySize);
    std::memcpy(animationEntries, gameAnimations, gameAnimationCount * animationEntrySize);
    for (int chosen = 0; chosen < animationsPerButton; ++chosen) {
        uint8_t* entry = animationEntries[gameAnimationCount + chosen];
        std::memcpy(entry, animationEntries[gameAnimationCount - animationsPerButton + chosen], animationEntrySize);
        field<int>(entry, animationLayoutOffset) = stanceButtonLayout;
    }
    field<const void*>(layoutList, layoutEntriesOffset) = layoutEntries;
    field<uint32_t>(layoutList, layoutCountOffset) = layoutCount;
    field<const void*>(layoutList, animationEntriesOffset) = animationEntries;
    field<uint32_t>(layoutList, animationCountOffset) = animationCount;
}

void setRuntimePatchBytes(const GameImage& game) {
    uint8_t setList[] = {0xc7, 0x46, 0x08, 0, 0, 0, 0};
    const auto listAddress = reinterpret_cast<uint32_t>(layoutList);
    std::memcpy(setList + 3, &listAddress, sizeof(listAddress));
    patches[layoutListPatch].setPatchedBytes(setList);

    constexpr uint32_t jumpLength = 5;
    const uintptr_t jumpFrom = game.address(patches[drawLabelPatch].preferredAddress());
    const auto offset = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&drawButtonLabel) - (jumpFrom + jumpLength));
    uint8_t jump[] = {0xe9, 0, 0, 0, 0, 0x90};
    std::memcpy(jump + 1, &offset, sizeof(offset));
    patches[drawLabelPatch].setPatchedBytes(jump);
}

// Auto-Select opens the choice window and waits for it as openChoiceWindow and stanceListInput do.
bool choiceWindowMatches(const GameImage& game) {
    const uint8_t windowOfHolder[] = {0x8b, 0x46, 0x70, 0x33, 0xdb, 0x89, 0x87, 0x48, 0x04, 0x00, 0x00};  // 0x5cbd90
    const uint8_t choicesOnly[] = {0xc7, 0x45, 0xd0, 0x02, 0x00, 0x00, 0x00};                          // 0x5cbdc7
    const uint8_t waitsForClosed[] = {0x83, 0x7f, 0x38, 0x04};                                          // 0x5cba9c
    const uint8_t chosenChoice[] = {0x8b, 0x87, 0xa0, 0x00, 0x00, 0x00};                                // 0x5be370
    // The window's filling adds a choice: mov edi,[esp+0x14] (window); push edx; mov edx,1 (list)
    const uint8_t addChoice[] = {0x8b, 0x7c, 0x24, 0x14, 0x52, 0xba, 0x01, 0x00, 0x00, 0x00};  // 0x5be5ed
    return game.codeIs(0x5be5ed, addChoice, sizeof(addChoice)) && game.callsTo(0x5be5f7, 0x5bdb20) &&
           game.codeIs(0x5cbd90, windowOfHolder, sizeof(windowOfHolder)) && game.operandIs(0x5cbd69, 0x9ebb8c) &&
           game.codeIs(0x5cbdc7, choicesOnly, sizeof(choicesOnly)) &&
           game.codeIs(0x5cba9c, waitsForClosed, sizeof(waitsForClosed)) &&
           game.codeIs(0x5be370, chosenChoice, sizeof(chosenChoice)) && game.callsTo(0x5cbe52, 0x5bda60) &&
           game.callsTo(0x5cbe5c, 0x5be410) && game.callsTo(0x5cbe63, 0x5bddf0);
}

// The code this part relies on, as in the Steam version (PE time stamp 683d42eb).
bool codeMatches(const GameImage& game) {
    // lea eax,[ebx+5]; mov dword ptr [esi+8],0x979664 (the relocated address is checked below)
    const uint8_t setLayoutList[] = {0x8d, 0x43, 0x05, 0xc7, 0x46, 0x08};
    // mov eax,[esp+0x24] (i); sub esi,-0x80; mov [esp+0x10],esi (the next label); cmp eax,4; jl
    const uint8_t labelLoopEnd[] = {0x8b, 0x44, 0x24, 0x24, 0x83, 0xee, 0x80, 0x89, 0x74, 0x24, 0x10, 0x83, 0xf8,
        0x04, 0x0f, 0x8c};
    // inc eax; push 1; mov [esp+0x28],eax (i); push eax
    const uint8_t labelLoopStart[] = {0x40, 0x6a, 0x01, 0x89, 0x44, 0x24, 0x28, 0x50};
    // mov esi,[esp+0x10]; test esi,esi; je 0x5ca93a
    const uint8_t labelText[] = {0x8b, 0x74, 0x24, 0x10, 0x85, 0xf6, 0x0f, 0x84, 0xb7, 0x00, 0x00, 0x00};
    const uint8_t placeLoopEnd[] = {0x83, 0xfb, 0x14, 0x7c, 0xd8};      // cmp ebx,0x14; jl
    const uint8_t highlightLoopEnd[] = {0x83, 0xfe, 0x04, 0x7c, 0xe7};  // cmp esi,4; jl
    // The menu's input: or ecx,-1; push esi; push edi; cmp dword ptr [ebx+0x46c],ecx
    const uint8_t menuStart[] = {0x83, 0xc9, 0xff, 0x56, 0x57, 0x39, 0x8b, 0x6c, 0x04, 0x00, 0x00};
    // The menu's cursor: cmp dword ptr [ebx+0x43c],4 after inc (down) and mov ...,3 (up)
    const uint8_t menuDown[] = {0xff, 0x83, 0x3c, 0x04, 0x00, 0x00, 0x83, 0xbb, 0x3c, 0x04, 0x00, 0x00, 0x04};
    const uint8_t menuUp[] = {0xc7, 0x83, 0x3c, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00};
    // The cursor highlight: lea eax,[eax+edi*2] picks animation 1 + 2i + (i != cursor)
    const uint8_t cursorAnimation[] = {0x8d, 0x04, 0x78};
    const auto* list = reinterpret_cast<const uint8_t*>(game.address(gameLayoutList));
    const auto* animations = *reinterpret_cast<const uint8_t* const*>(list + animationEntriesOffset);
    const auto* layouts = *reinterpret_cast<const uint8_t* const*>(list + layoutEntriesOffset);
    return game.codeIs(0x5ca10c, setLayoutList, sizeof(setLayoutList)) && game.operandIs(0x5ca112, gameLayoutList) &&
           game.codeIs(0x5ca93a, labelLoopEnd, sizeof(labelLoopEnd)) &&
           game.codeIs(0x5ca85b, labelLoopStart, sizeof(labelLoopStart)) &&
           game.codeIs(0x5ca877, labelText, sizeof(labelText)) &&
           game.codeIs(0x5caf10, placeLoopEnd, sizeof(placeLoopEnd)) &&
           game.codeIs(0x5caf34, highlightLoopEnd, sizeof(highlightLoopEnd)) &&
           game.codeIs(0x5cb1b7, menuStart, sizeof(menuStart)) && game.codeIs(0x5cb55b, menuDown, sizeof(menuDown)) &&
           game.codeIs(0x5cb517, menuUp, sizeof(menuUp)) && game.codeIs(0x5cb168, cursorAnimation, sizeof(cursorAnimation)) &&
           choiceWindowMatches(game) &&
           field<uint32_t>(const_cast<uint8_t*>(list), layoutCountOffset) == gameLayoutCount &&
           field<uint32_t>(const_cast<uint8_t*>(list), animationCountOffset) == gameAnimationCount &&
           std::strcmp(reinterpret_cast<const char*>(layouts + (gameLayoutCount - 1) * layoutEntrySize),
               "wazaFunc.layout") == 0 &&
           field<int>(const_cast<uint8_t*>(animations + (gameAnimationCount - 1) * animationEntrySize),
               animationLayoutOffset) == lastGameButton + 1;
}

struct MenuHook {
    uintptr_t preferred;
    void* detour;
    void** original;
};

}  // namespace

bool installStanceMenu(const wots4::ModContext& context, const GameImage& game, const TweakLog& log) {
    if (!codeMatches(game)) {
        log.installFailed("the My Style move select menu is not the known version", 0x5ca10c);
        return false;
    }
    host = context.host;
    copyLayoutList(game);
    setRuntimePatchBytes(game);
    drawLabelReturn = game.address(0x5ca87d);
    decideKeys = reinterpret_cast<const uint32_t*>(game.address(0x9db500));
    cancelKeys = reinterpret_cast<const uint32_t*>(game.address(0x9db504));
    upKeys = reinterpret_cast<const uint32_t*>(game.address(0x9db5cc));
    downKeys = reinterpret_cast<const uint32_t*>(game.address(0x9db5d0));
    menuLeaveHolder = game.address(0x9ecf30);
    gameMenuInput = game.address(0x5cb1a0);
    if (!claimPatches(context, game, patches, _countof(patches), log)) return false;

    const MenuHook hooks[] = {
        {0x5ca460, reinterpret_cast<void*>(&placeButtons), reinterpret_cast<void**>(&originalPlaceButtons)},
        {0x5cb070, reinterpret_cast<void*>(&highlightButton), reinterpret_cast<void**>(&originalHighlightButton)},
        {0x5cb110, reinterpret_cast<void*>(&highlightCursor), reinterpret_cast<void**>(&originalHighlightCursor)},
        {0x5cb1a0, reinterpret_cast<void*>(&menuInput), reinterpret_cast<void**>(&originalMenuInput)},
        {0x5c9ea0, reinterpret_cast<void*>(&releaseShared), &originalReleaseShared},
    };
    constexpr uint32_t hookLength = 5;
    for (size_t i = 0; i < _countof(hooks); ++i) {
        CodeChangeError error = hookGameCode(context, game, hooks[i].preferred, hookLength, hooks[i].detour, hooks[i].original);
        if (!error) continue;
        for (size_t done = 0; done < i; ++done) unhookGameCode(game, hooks[done].preferred);
        log.installFailed(error, hooks[i].preferred);
        return false;
    }
    return true;
}

void setStanceMenuEnabled(bool enabled, const TweakLog& log) {
    writePatches(patches, _countof(patches), enabled, log);
}

}  // namespace tweaks::custom_style_stance
