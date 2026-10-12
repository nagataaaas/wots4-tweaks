// Installs mod.dll on a copy of the exe mapped into this process and runs the two hooked
// instructions of the item draw 0x4f50b0 with a synthetic stack frame. The rest of the draw
// needs the running game, so on the copy the instruction after each hooked one returns at once.
#include <cstdio>
#include <vector>
#include "mapped_exe.h"

using namespace mapped_exe;

namespace {

constexpr uintptr_t dropRateDivisor = 0x4f5173;
constexpr uintptr_t candidateRollDivisor = 0x4f5190;
constexpr uint32_t divisorLength = 5;
constexpr uintptr_t lootReturn = 0x493359;
constexpr uintptr_t kickReturn = 0x51cbb4;
constexpr uintptr_t deathDropReturn = 0x49c07e;  // A townsperson dropping an item by itself
constexpr uintptr_t firstWeightInstruction = 0x4f519b;

// A stack frame of the draw: the return address above ebp, the candidates' weights below it.
class DrawFrame {
public:
    explicit DrawFrame(uintptr_t returnAddress) : bytes(frameSize) {
        *reinterpret_cast<uint32_t*>(ebp() + 4) = static_cast<uint32_t>(va(returnAddress));
    }
    uint8_t* ebp() { return bytes.data() + belowFrame; }
    void setWeight(uint32_t candidate, int16_t weight) {
        *reinterpret_cast<int16_t*>(ebp() - 0x40 + candidate * 6) = weight;
    }

private:
    static constexpr size_t belowFrame = 0x60;
    static constexpr size_t frameSize = belowFrame + 8;
    std::vector<uint8_t> bytes;
};

struct Registers {
    uint32_t eaxValue, ebxValue, ecxValue, edxValue;
};

// Runs the hooked instruction at preferred with ebp = the frame, eax = the roll and ebx = the rate.
// ebp is the draw's frame for the call only, and is restored before any local is touched.
#pragma warning(suppress : 4731)
Registers runHookedDivisor(uintptr_t preferred, DrawFrame& frame, uint32_t roll, uint32_t rate) {
    const uintptr_t function = va(preferred);
    uint8_t* framePointer = frame.ebp();
    uint32_t eaxAfter, ebxAfter, ecxAfter, edxAfter;
    __asm {
        push ebx
        push esi
        push edi
        mov esi, function
        mov edi, framePointer
        mov eax, roll
        mov ebx, rate
        xor ecx, ecx
        xor edx, edx
        push ebp
        mov ebp, edi
        call esi
        pop ebp
        mov eaxAfter, eax
        mov ebxAfter, ebx
        mov ecxAfter, ecx
        mov edxAfter, edx
        pop edi
        pop esi
        pop ebx
    }
    return {eaxAfter, ebxAfter, ecxAfter, edxAfter};
}

uint32_t rateAfterHook(uintptr_t returnAddress, uint32_t rate) {
    DrawFrame frame(returnAddress);
    return runHookedDivisor(dropRateDivisor, frame, 57, rate).ebxValue;
}

}  // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3) return 2;
    if (!mapImage(argv[1])) {
        std::printf("Could not map the exe copy %s; no test was run.\n", argv[1]);
        return 3;
    }
    if (!createTestDirectory(L"wots4_loot_drop")) {
        std::printf("Could not create the test folder under %%TEMP%%; no test was run.\n");
        return 3;
    }
    check(loadMod(argv[2]), "mod.dll exports Wots4ModInitialize");
    if (!modInitialize) return 1;

    // A different game: the tweak is skipped and nothing is hooked.
    codeByte(firstWeightInstruction + 2) = 0xc4;
    check(initializeMod(), "with changed game code the Mod still initializes");
    check(loggedText("Guaranteed Loot: the item draw is not the known version at 0x4f50b0; the tweak is skipped") &&
              loggedText("Tweak loot_drop could not be installed"),
        "the skipped tweak is logged");
    check(!claimedAt(dropRateDivisor) && !claimedAt(candidateRollDivisor) &&
              optionIndex(L"Guaranteed Loot") == noItem && codeByte(dropRateDivisor) == 0xb9 &&
              codeByte(candidateRollDivisor) == 0xb9,
        "nothing is claimed, hooked or shown");
    codeByte(firstWeightInstruction + 2) = 0xc0;

    check(initializeMod(), "the Mod initializes on the known game code");
    check(claimed(dropRateDivisor, divisorLength) && claimed(candidateRollDivisor, divisorLength),
        "0x4f5173 and 0x4f5190 are claimed (5 bytes each)");
    check(codeByte(dropRateDivisor) == jumpOpcode && codeByte(candidateRollDivisor) == jumpOpcode,
        "the hooks are written at both rolls");
    const uint32_t item = optionIndex(L"Guaranteed Loot");
    check(item != noItem && optionItem(item).kind == wots4::optionToggle &&
              std::wstring(optionItem(item).label.japanese) == L"漁り確定ドロップ",
        "an on / off option is passed in both languages");
    if (item == noItem) return 1;
    check(optionValue(item) == 0, "without config.ini the tweak is off");

    codeByte(dropRateDivisor + divisorLength) = returnOpcode;
    codeByte(candidateRollDivisor + divisorLength) = returnOpcode;

    DrawFrame offFrame(lootReturn);
    const Registers off = runHookedDivisor(dropRateDivisor, offFrame, 57, 10);
    check(off.ebxValue == 10 && off.eaxValue == 57 && off.ecxValue == 100,
        "off: looting keeps the rate, the roll, and runs the moved mov ecx,100");

    changeOption(item, 1);
    check(rateAfterHook(lootReturn, 10) == 100, "on: looting draws with a 100% rate");
    check(rateAfterHook(kickReturn, 30) == 100, "on: kicking draws with a 100% rate");
    check(rateAfterHook(deathDropReturn, 10) == 10, "on: a townsperson's own drop keeps its rate");

    DrawFrame looting(lootReturn);
    looting.setWeight(0, 33);
    looting.setWeight(1, 33);
    looting.setWeight(2, 33);
    const Registers landed = runHookedDivisor(candidateRollDivisor, looting, 99, 0);
    check(landed.eaxValue == 0 && landed.edxValue == 0 && landed.ecxValue == 100,
        "on: a roll of 99 past weights adding up to 99 lands on the first candidate");
    check(runHookedDivisor(candidateRollDivisor, looting, 250, 0).eaxValue == 250 % 99,
        "on: a roll is taken within the weights' total, keeping the candidates' odds");
    DrawFrame nothingLeft(lootReturn);
    check(runHookedDivisor(candidateRollDivisor, nothingLeft, 42, 0).eaxValue == 42,
        "on: with no candidate left the roll is kept");
    DrawFrame deathDrop(deathDropReturn);
    deathDrop.setWeight(0, 50);
    check(runHookedDivisor(candidateRollDivisor, deathDrop, 77, 0).eaxValue == 77,
        "on: a townsperson's own drop keeps its roll");

    changeOption(item, 0);
    check(runHookedDivisor(candidateRollDivisor, looting, 99, 0).eaxValue == 99, "off again: looting keeps its roll");

    changeOption(item, 1);
    check(optionsSavedCallback != nullptr, "the options save is watched");
    if (optionsSavedCallback) optionsSavedCallback(optionsSavedContext);
    check(GetPrivateProfileIntW(L"loot_drop", L"enabled", 99, configPath().c_str()) == 1,
        "the options save writes [loot_drop] enabled=1");

    return finish("loot_drop_native_probe");
}
