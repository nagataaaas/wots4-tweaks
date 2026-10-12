// The item draw 0x4f50b0 that looting and kicking a body share. It rolls twice:
// rand() % 100 < the drop rate, then rand() % 100 against the candidates' weights added up
// in order. The weights of a row add up to 100, but candidates that cannot drop are left out
// and their weight shared with integer division, so the second roll can miss too.
#pragma once
#include <cstdint>

namespace tweaks::loot_drop {

constexpr uint32_t certainDropRate = 100;  // Percent; the first roll is 0 to 99

// The draw keeps 10 candidates on its stack frame: candidate k has its int16 weight at
// ebp - 0x40 + 6k (lea ecx,[ebp-0x40] at 0x4f519b); unused ones weigh 0.
constexpr uint32_t candidateCount = 10;
constexpr uint32_t firstWeightBelowFrame = 0x40;
constexpr uint32_t candidateSize = 6;

inline int32_t candidateWeightTotal(const uint8_t* frame) {
    int32_t total = 0;
    const uint8_t* weight = frame - firstWeightBelowFrame;
    for (uint32_t index = 0; index < candidateCount; ++index, weight += candidateSize)
        total += *reinterpret_cast<const int16_t*>(weight);
    return total;
}

// A second roll that always lands on a candidate, keeping their odds: within [0, total).
// With no candidate left (total 0) nothing can drop, so the roll is kept.
inline uint32_t rollWithinCandidates(const uint8_t* frame, uint32_t roll) {
    const int32_t total = candidateWeightTotal(frame);
    return total > 0 ? roll % static_cast<uint32_t>(total) : roll;
}

}  // namespace tweaks::loot_drop
