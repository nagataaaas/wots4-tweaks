// Stand-in tweak list for options_menu_tests: replaces src/tweak_list.cpp in the test build.
#include "test_tweak_list.h"

namespace testing {

uint32_t alphaValues[alphaOptionCount];
uint32_t betaValues[betaOptionCount];
uint32_t betaAppliedCount;
uint32_t betaLastApplied;
uint32_t failingValue;
uint32_t malformedValue;
bool malformedInstallCalled;

namespace {

void betaApplied(uint32_t value) {
    ++betaAppliedCount;
    betaLastApplied = value;
}

// ModInitialize takes the language from the test machine, so these texts are the same in both.
tweaks::Text inBoth(const wchar_t* text) {
    return {text, text};
}

const tweaks::Text sizes[] = {inBoth(L"Small"), inBoth(L"Medium"), inBoth(L"Large")};

const tweaks::Option alphaOptions[alphaOptionCount] = {
    tweaks::toggle(L"first", inBoth(L"Alpha 1"), &alphaValues[0], true),
    tweaks::toggle(L"second", inBoth(L"Alpha 2"), &alphaValues[1], true),
    tweaks::toggle(L"third", inBoth(L"Alpha 3"), &alphaValues[2], false),
    tweaks::toggle(L"fourth", inBoth(L"Alpha 4"), &alphaValues[3], false),
    tweaks::toggle(L"fifth", inBoth(L"Alpha 5"), &alphaValues[4], false),
};
const tweaks::Option betaOptions[betaOptionCount] = {
    tweaks::choice(L"first", inBoth(L"Beta 1"), sizes, &betaValues[0], 1, betaApplied),
    tweaks::choice(L"second", inBoth(L"Beta 2"), sizes, &betaValues[1], 1, betaApplied),
    tweaks::choice(L"third", inBoth(L"Beta 3"), sizes, &betaValues[2], 0, betaApplied),
    tweaks::choice(L"fourth", inBoth(L"Beta 4"), sizes, &betaValues[3], 2, betaApplied),
};
const tweaks::Option failingOptions[] = {tweaks::toggle(L"enabled", inBoth(L"Failing"), &failingValue, true)};
// The default is outside the choices, so the definition is rejected before install.
const tweaks::Option malformedOptions[] = {
    {wots4::optionChoice, L"mode", inBoth(L"Malformed"), sizes, 3, 3, &malformedValue, nullptr},
};

bool installFailing(const wots4::ModContext&) { return false; }
bool installMalformed(const wots4::ModContext&) {
    malformedInstallCalled = true;
    return true;
}

const tweaks::Tweak alpha{L"alpha", alphaOptions, alphaOptionCount, nullptr};
const tweaks::Tweak failing{L"failing", failingOptions, 1, installFailing};
const tweaks::Tweak beta{L"beta", betaOptions, betaOptionCount, nullptr};
const tweaks::Tweak malformed{L"malformed", malformedOptions, 1, installMalformed};

}  // namespace
}  // namespace testing

namespace tweaks {

std::vector<const Tweak*> registeredTweaks() {
    return {&testing::alpha, &testing::failing, &testing::beta, &testing::malformed};
}

}  // namespace tweaks
