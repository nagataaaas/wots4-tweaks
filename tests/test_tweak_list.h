// The tweaks of test_tweak_list.cpp, in menu order: alpha (5 toggles), failing (install fails),
// beta (4 choices of 3), malformed (rejected definition).
#pragma once
#include "../src/tweak_list.h"

namespace testing {

constexpr uint32_t alphaOptionCount = 5;
constexpr uint32_t betaOptionCount = 4;

extern uint32_t alphaValues[alphaOptionCount];
extern uint32_t betaValues[betaOptionCount];
extern uint32_t betaAppliedCount;
extern uint32_t betaLastApplied;
extern bool malformedInstallCalled;

}  // namespace testing
