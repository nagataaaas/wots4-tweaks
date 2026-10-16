// Things on the ground (ItemEntitySpawner.cpp, [0x9e9694]) live in one 0x6a0-byte object: the
// vtable, two counts, 60 places of 0x1c bytes from +8, then a list head at +0x698. The game makes it
// at startup and on some area loads (0x4f7c1b, 0x553a1e, 0x4f8243) and frees it with its size
// (0x4f5bf1). To have more places, the object is made bigger, the list head moves behind the
// places, and every place of the code that counts on 60 is changed. Changing that while an object
// exists would break it, so this writes once, when the game starts, before the object is made.
//
// Most counts are 32-bit operands and are rewritten in place. The loop checks "cmp reg, 60; jl"
// and "push 60" have 8-bit signed operands, too small for 240, so they jump to stubs that do the
// same with 32-bit operands. Place counts are multiples of 30: some loops go 5 places and some 6
// places a turn.
#include "ground_slots.h"
#include <cstdio>
#include <cstring>

namespace tweaks::ground_slots {

namespace {

constexpr uintptr_t spawnerGlobal = 0x9e9694;
constexpr uint32_t placeSize = 0x1c;
constexpr uint32_t placesOffset = 8;
constexpr uint32_t listHeadSize = 8;

constexpr uint32_t listOffset(uint32_t placeCount) { return placesOffset + placeCount * placeSize; }
constexpr uint32_t objectSize(uint32_t placeCount) { return listOffset(placeCount) + listHeadSize; }
static_assert(listOffset(gamePlaceCount) == 0x698 && objectSize(gamePlaceCount) == 0x6a0, "the game's layout");

// A 32-bit operand in the game's code and what it is in terms of the place count.
enum class Operand { objectSize, listOffset, placeCount, groupsOfFive, groupsOfSix };
struct OperandSite {
    uintptr_t instruction;
    uint32_t operandOffset;
    Operand meaning;
};
const OperandSite operandSites[] = {
    {0x4f5bf1, 1, Operand::objectSize},    // push 0x6a0 (free, in the destructor 0x4f5be0)
    {0x4f7c1b, 1, Operand::objectSize},    // push 0x6a0 (make)
    {0x553a1e, 1, Operand::objectSize},    // push 0x6a0 (make)
    {0x4f8243, 1, Operand::objectSize},    // push 0x6a0 (make)
    {0x4f5b80, 2, Operand::listOffset},    // lea edi, [ebx+0x698] (constructor)
    {0x4f5cb9, 2, Operand::listOffset},    // mov esi, [edi+0x698]
    {0x4f5cbf, 2, Operand::listOffset},    // lea ebx, [edi+0x698]
    {0x4f6632, 2, Operand::listOffset},    // add esi, 0x698
    {0x4f6f10, 2, Operand::listOffset},    // mov eax, [ecx+0x698]
    {0x4f6f16, 2, Operand::listOffset},    // add ecx, 0x698
    {0x4f7329, 2, Operand::listOffset},    // mov edi, [ebx+0x698]
    {0x4f732f, 2, Operand::listOffset},    // lea eax, [ebx+0x698]
    {0x4f7637, 2, Operand::listOffset},    // lea eax, [esi+0x698]
    {0x4f5b90, 1, Operand::placeCount},    // mov ecx, 60 (constructor: clear each place)
    {0x4f6ea4, 3, Operand::placeCount},    // mov dword ptr [ebp+8], 60
    {0x4f778e, 3, Operand::groupsOfFive},  // mov dword ptr [ebp-8], 12 (0x4f7740: 5 places a turn)
    {0x4f7131, 1, Operand::groupsOfSix},   // mov esi, 10 (6 places a turn)
};
constexpr size_t operandSiteCount = _countof(operandSites);

// "cmp reg, 60; jl back" ending a loop over the places. The jl is short (2 bytes) or near (6 bytes).
struct LoopCheckSite {
    uintptr_t instruction;
    uint8_t registerCode;   // Of cmp reg, imm8 (83 f8+reg)
    bool nearJump;
    uint32_t lastPlaceGap;  // The game compares with 60 - this (0x4f7909 stops one place early)
};
const LoopCheckSite loopCheckSites[] = {
    {0x4f5ca5, 6, false, 0},  // cmp esi, 60
    {0x4f6a59, 0, false, 0},  // cmp eax, 60
    {0x4f70ec, 2, true, 0},   // cmp edx, 60 (0x4f6f60: 6 places a turn, the index of the last + 1)
    {0x4f71ba, 6, false, 0},  // cmp esi, 60
    {0x4f721d, 6, false, 0},  // cmp esi, 60
    {0x4f7266, 0, false, 0},  // cmp eax, 60
    {0x4f771e, 0, false, 0},  // cmp eax, 60 (0x4f7710: find a free place)
    {0x4f7909, 6, false, 1},  // cmp esi, 59
    {0x4f7992, 6, false, 0},  // cmp esi, 60
    {0x4fcffe, 1, false, 0},  // cmp ecx, 60 (outside the class: find a thing's place)
    {0x522bb9, 1, false, 0},  // cmp ecx, 60 (outside the class: find a thing's place)
};
constexpr size_t loopCheckSiteCount = _countof(loopCheckSites);

// "push 60; push 0x1c" giving the place count to the array constructor and destructor
// (0x8d45ca and its pair), with the instruction that follows (moved to the stub as it is).
struct ArrayCountSite {
    uintptr_t instruction;
    uint8_t followingLength;
};
const ArrayCountSite arrayCountSites[] = {
    {0x4f5b6d, 3},  // push 60; push 0x1c; lea eax, [ebx+8] (constructor)
    {0x4f5d44, 1},  // push 60; push 0x1c; push edx (destructor)
};
constexpr size_t arrayCountSiteCount = _countof(arrayCountSites);

// The 8-bit "lea edx, [ecx+10]" (0x4f6f69, 6 places a turn); 240 / 6 still fits.
constexpr uintptr_t groupsOfSixDisplacement = 0x4f6f6b;
// The 8-bit "lea edx, [esi-9]" (0x4f7139) after "mov esi, 10": the loop at 0x4f7130 makes its step
// of 1, which is also the mark it writes, from its count, so the displacement follows the count.
constexpr uintptr_t markStepDisplacement = 0x4f713b;
constexpr uint8_t markStepFromGroupsOfSix(uint32_t groups) { return static_cast<uint8_t>(1 - groups); }

constexpr uint8_t jumpOpcode = 0xe9;
constexpr uint32_t jumpLength = 5;
constexpr uint8_t nop = 0x90;
constexpr uint32_t shortLoopCheckLength = 5;  // cmp reg, imm8 (3) + jl rel8 (2)
constexpr uint32_t nearLoopCheckLength = 9;   // cmp reg, imm8 (3) + jl rel32 (6)
constexpr uint32_t arrayCountLength = 4;      // push imm8 (2) + push imm8 (2)

constexpr size_t patchCount = operandSiteCount + loopCheckSiteCount + arrayCountSiteCount + 2;
CodePatch patches[patchCount] = {
    // Operands; the bytes are set when the game starts.
    CodePatch(0x4f5bf2, {0, 0, 0, 0}), CodePatch(0x4f7c1c, {0, 0, 0, 0}), CodePatch(0x553a1f, {0, 0, 0, 0}),
    CodePatch(0x4f8244, {0, 0, 0, 0}), CodePatch(0x4f5b82, {0, 0, 0, 0}), CodePatch(0x4f5cbb, {0, 0, 0, 0}),
    CodePatch(0x4f5cc1, {0, 0, 0, 0}), CodePatch(0x4f6634, {0, 0, 0, 0}), CodePatch(0x4f6f12, {0, 0, 0, 0}),
    CodePatch(0x4f6f18, {0, 0, 0, 0}), CodePatch(0x4f732b, {0, 0, 0, 0}), CodePatch(0x4f7331, {0, 0, 0, 0}),
    CodePatch(0x4f7639, {0, 0, 0, 0}), CodePatch(0x4f5b91, {0, 0, 0, 0}), CodePatch(0x4f6ea7, {0, 0, 0, 0}),
    CodePatch(0x4f7791, {0, 0, 0, 0}), CodePatch(0x4f7132, {0, 0, 0, 0}),
    // Loop checks: a jump to the stub, then nops.
    CodePatch(0x4f5ca5, {0, 0, 0, 0, 0}), CodePatch(0x4f6a59, {0, 0, 0, 0, 0}),
    CodePatch(0x4f70ec, {0, 0, 0, 0, 0, 0, 0, 0, 0}), CodePatch(0x4f71ba, {0, 0, 0, 0, 0}),
    CodePatch(0x4f721d, {0, 0, 0, 0, 0}), CodePatch(0x4f7266, {0, 0, 0, 0, 0}), CodePatch(0x4f771e, {0, 0, 0, 0, 0}),
    CodePatch(0x4f7909, {0, 0, 0, 0, 0}), CodePatch(0x4f7992, {0, 0, 0, 0, 0}), CodePatch(0x4fcffe, {0, 0, 0, 0, 0}),
    CodePatch(0x522bb9, {0, 0, 0, 0, 0}),
    // Array counts: a jump to the stub, then nops.
    CodePatch(0x4f5b6d, {0, 0, 0, 0, 0, 0, 0}), CodePatch(0x4f5d44, {0, 0, 0, 0, 0}),
    CodePatch(groupsOfSixDisplacement, {0}), CodePatch(markStepDisplacement, {0}),
};
constexpr size_t firstLoopCheckPatch = operandSiteCount;
constexpr size_t firstArrayCountPatch = firstLoopCheckPatch + loopCheckSiteCount;
constexpr size_t groupsOfSixPatch = firstArrayCountPatch + arrayCountSiteCount;
constexpr size_t markStepPatch = groupsOfSixPatch + 1;

GameImage game;
const TweakLog* logger = nullptr;
bool installed = false;
bool startupApplied = false;

uint32_t operandValue(Operand meaning, uint32_t placeCount) {
    switch (meaning) {
    case Operand::objectSize: return objectSize(placeCount);
    case Operand::listOffset: return listOffset(placeCount);
    case Operand::placeCount: return placeCount;
    case Operand::groupsOfFive: return placeCount / 5;
    case Operand::groupsOfSix: return placeCount / 6;
    }
    return 0;
}

// The code this tweak relies on, as in the Steam version (PE time stamp 683d42eb).
bool operandsMatch() {
    for (const OperandSite& site : operandSites) {
        const uint32_t expected = operandValue(site.meaning, gamePlaceCount);
        if (game.readUint32(site.instruction + site.operandOffset) != expected) return false;
    }
    return true;
}

bool loopChecksMatch() {
    constexpr uint8_t compareOpcode = 0x83;
    constexpr uint8_t compareModRm = 0xf8;  // cmp reg, imm8 (/7)
    constexpr uint8_t shortJl = 0x7c;
    const uint8_t nearJl[] = {0x0f, 0x8c};
    for (const LoopCheckSite& site : loopCheckSites) {
        const uint8_t compare[] = {compareOpcode, static_cast<uint8_t>(compareModRm | site.registerCode),
            static_cast<uint8_t>(gamePlaceCount - site.lastPlaceGap)};
        if (!game.codeIs(site.instruction, compare, sizeof(compare))) return false;
        const bool jumpMatches = site.nearJump ? game.codeIs(site.instruction + 3, nearJl, sizeof(nearJl))
                                               : game.codeIs(site.instruction + 3, &shortJl, 1);
        if (!jumpMatches) return false;
    }
    return true;
}

bool codeMatches() {
    const uint8_t arrayCounts[] = {0x6a, gamePlaceCount, 0x6a, placeSize};
    const uint8_t constructorFollowing[] = {0x8d, 0x43, 0x08};  // lea eax, [ebx+8]
    const uint8_t destructorFollowing[] = {0x52};               // push edx
    const uint8_t groupsOfSix[] = {0x8d, 0x51, gamePlaceCount / 6};  // lea edx, [ecx+10]
    const uint8_t markStep[] = {0x8d, 0x56, markStepFromGroupsOfSix(gamePlaceCount / 6)};  // lea edx, [esi-9]
    return operandsMatch() && loopChecksMatch() &&
           game.codeIs(arrayCountSites[0].instruction, arrayCounts, sizeof(arrayCounts)) &&
           game.codeIs(arrayCountSites[0].instruction + arrayCountLength, constructorFollowing,
               sizeof(constructorFollowing)) &&
           game.codeIs(arrayCountSites[1].instruction, arrayCounts, sizeof(arrayCounts)) &&
           game.codeIs(arrayCountSites[1].instruction + arrayCountLength, destructorFollowing,
               sizeof(destructorFollowing)) &&
           game.codeIs(groupsOfSixDisplacement - 2, groupsOfSix, sizeof(groupsOfSix)) &&
           game.codeIs(markStepDisplacement - 2, markStep, sizeof(markStep));
}

// patches lists the sites in the order of the tables above; a mistake there would write one site's
// bytes over another.
bool patchesFollowSites() {
    for (size_t index = 0; index < operandSiteCount; ++index)
        if (patches[index].preferredAddress() != operandSites[index].instruction + operandSites[index].operandOffset)
            return false;
    for (size_t index = 0; index < loopCheckSiteCount; ++index)
        if (patches[firstLoopCheckPatch + index].preferredAddress() != loopCheckSites[index].instruction) return false;
    for (size_t index = 0; index < arrayCountSiteCount; ++index)
        if (patches[firstArrayCountPatch + index].preferredAddress() != arrayCountSites[index].instruction) return false;
    return patches[groupsOfSixPatch].preferredAddress() == groupsOfSixDisplacement &&
           patches[markStepPatch].preferredAddress() == markStepDisplacement;
}

// ---------------------------------------------------------------- stubs

class StubWriter {
public:
    explicit StubWriter(uint8_t* memory) : next(memory) {}
    uint8_t* position() const { return next; }
    void byte(uint8_t value) { *next++ = value; }
    void bytes(const void* source, size_t length) {
        std::memcpy(next, source, length);
        next += length;
    }
    void uint32(uint32_t value) { bytes(&value, sizeof(value)); }
    // rel32 to target, from the end of the 4 bytes written.
    void relative(uintptr_t target) { uint32(static_cast<uint32_t>(target - reinterpret_cast<uintptr_t>(next + 4))); }

private:
    uint8_t* next;
};

// The jump written over the game's instructions; the patch's length is theirs, the rest is nops.
void setJumpTo(CodePatch& patch, uintptr_t from, const uint8_t* stub) {
    uint8_t bytes[CodePatch::maxLength];
    std::memset(bytes, nop, sizeof(bytes));
    bytes[0] = jumpOpcode;
    const auto offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stub) - (from + jumpLength));
    std::memcpy(bytes + 1, &offset, sizeof(offset));
    patch.setPatchedBytes(bytes);
}

// cmp reg, imm32; jl <the game's target>; jmp <after the game's jl>
void writeLoopCheckStub(StubWriter& writer, const LoopCheckSite& site, size_t patchIndex, uint32_t placeCount) {
    const uintptr_t start = game.address(site.instruction);
    const uint32_t length = site.nearJump ? nearLoopCheckLength : shortLoopCheckLength;
    const uintptr_t after = start + length;
    const uintptr_t target = site.nearJump
                                 ? after + static_cast<int32_t>(game.readUint32(site.instruction + 5))
                                 : after + *reinterpret_cast<const int8_t*>(start + 4);
    setJumpTo(patches[patchIndex], start, writer.position());
    writer.byte(0x81);
    writer.byte(static_cast<uint8_t>(0xf8 | site.registerCode));
    writer.uint32(placeCount - site.lastPlaceGap);
    writer.byte(0x0f);
    writer.byte(0x8c);
    writer.relative(target);
    writer.byte(jumpOpcode);
    writer.relative(after);
}

// push imm32; push 0x1c; <the following instruction>; jmp <after it>
void writeArrayCountStub(StubWriter& writer, const ArrayCountSite& site, size_t patchIndex, uint32_t placeCount) {
    const uintptr_t start = game.address(site.instruction);
    const uint32_t length = arrayCountLength + site.followingLength;
    setJumpTo(patches[patchIndex], start, writer.position());
    writer.byte(0x68);
    writer.uint32(placeCount);
    writer.byte(0x6a);
    writer.byte(placeSize);
    writer.bytes(reinterpret_cast<const void*>(start + arrayCountLength), site.followingLength);
    writer.byte(jumpOpcode);
    writer.relative(start + length);
}

constexpr size_t stubMemorySize = 0x1000;

bool preparePatches(uint32_t placeCount) {
    auto* stubs = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, stubMemorySize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stubs) return false;
    for (size_t index = 0; index < operandSiteCount; ++index) {
        const uint32_t value = operandValue(operandSites[index].meaning, placeCount);
        patches[index].setPatchedBytes(reinterpret_cast<const uint8_t*>(&value));  // x86: little-endian
    }
    StubWriter writer(stubs);
    for (size_t index = 0; index < loopCheckSiteCount; ++index)
        writeLoopCheckStub(writer, loopCheckSites[index], firstLoopCheckPatch + index, placeCount);
    for (size_t index = 0; index < arrayCountSiteCount; ++index)
        writeArrayCountStub(writer, arrayCountSites[index], firstArrayCountPatch + index, placeCount);
    const auto groupsOfSix = static_cast<uint8_t>(placeCount / 6);
    patches[groupsOfSixPatch].setPatchedBytes(&groupsOfSix);
    const uint8_t markStep = markStepFromGroupsOfSix(groupsOfSix);
    patches[markStepPatch].setPatchedBytes(&markStep);
    FlushInstructionCache(GetCurrentProcess(), stubs, stubMemorySize);
    return true;
}

}  // namespace

bool install(const wots4::ModContext& context, const GameImage& gameImage, const TweakLog& log) {
    game = gameImage;
    logger = &log;
    if (!patchesFollowSites()) {
        log.installFailed("the Mod's list of places to change is out of order", 0x4f5b60);
        return false;
    }
    if (!codeMatches()) {
        log.installFailed("the places for things on the ground are not the known version", 0x4f5b60);
        return false;
    }
    if (!claimPatches(context, game, patches, patchCount, log)) return false;
    installed = true;
    return true;
}

void applyAtStartup(uint32_t placeCount) {
    if (!installed || startupApplied) return;
    startupApplied = true;
    if (placeCount == gamePlaceCount) return;
    if (game.readUint32(spawnerGlobal) != 0) {
        logger->write("WARN", "the game made the places for things on the ground before the Mod started; "
                              "keeping the game's 60 places");
        return;
    }
    if (!preparePatches(placeCount)) {
        logger->write("ERROR", "could not allocate memory for the code of the places; keeping the game's 60 places");
        return;
    }
    writePatches(patches, patchCount, true, *logger);
    char line[96];
    sprintf_s(line, "the ground has %u places for things", placeCount);
    logger->write("INFO", line);
}

}  // namespace tweaks::ground_slots
