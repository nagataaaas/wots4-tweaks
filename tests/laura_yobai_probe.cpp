// Checks, with synthetic game objects, how the Laura mark is set for each setting.
#include <cstdio>
#include "synthetic_definitions.h"

using namespace synthetic;

namespace {

int passed, failed;
void check(bool condition, const char* name) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
    condition ? ++passed : ++failed;
}

MarkUpdate startVisit(GameObjects& objects, uint32_t setting) {
    return updateLauraMark(setting, objects.manager.data(), objects.gameState.data(), objects.definitionRoot.data());
}

void testDefinitionLookup() {
    GameObjects objects;
    const uint8_t* root = objects.definitionRoot.data();
    check(findPersonDefinition(root, lauraDefinition) == objects.table.data() + definitionOffsetsStart +
                                                            definitionCount * 4,
        "the definition is at the table plus its offset");
    check(!findPersonDefinition(root, missingDefinition), "an offset of 0 is no definition");
    check(!findPersonDefinition(root, definitionCount), "an ID at the count is outside the table");
    check(!findPersonDefinition(root, noPromise), "a negative ID is no definition");
    check(!findPersonDefinition(nullptr, lauraDefinition), "no definition root, no definition");
    objects.definitionRoot[0] = 3;
    check(!findPersonDefinition(root, lauraDefinition), "a root that does not hold the table (not 4) is not read");
}

void testAsTheGameDecides() {
    GameObjects objects;
    objects.promise(otherDefinition);
    objects.mark() = markSet;
    const MarkUpdate update = startVisit(objects, partnerAsTheGameDecides);
    check(!update.written && objects.mark() == markSet, "as the game decides, a stale mark is left as it is");
}

void testFake() {
    GameObjects objects;
    objects.promise(lauraDefinition);
    objects.mark() = markUnset;
    MarkUpdate update = startVisit(objects, partnerFake);
    check(update.written && update.before == markUnset && objects.mark() == markSet,
        "fake: Laura's promise after a restart (mark 0) sets the mark");
    update = startVisit(objects, partnerFake);
    check(update.written && update.before == update.after && objects.mark() == markSet,
        "fake: a mark that is already right stays");
    objects.promise(otherDefinition);
    update = startVisit(objects, partnerFake);
    check(update.written && update.promisedDefinition == otherDefinition && objects.mark() == markUnset,
        "fake: another partner's promise clears a stale mark");
}

void testReal() {
    GameObjects objects;
    objects.promise(lauraDefinition);
    objects.mark() = markSet;
    const MarkUpdate update = startVisit(objects, partnerReal);
    check(update.written && update.before == markSet && objects.mark() == markUnset,
        "real: Laura's promise made in this session (mark 1) clears the mark");
    objects.promise(otherDefinition);
    objects.mark() = markSet;
    startVisit(objects, partnerReal);
    check(objects.mark() == markUnset, "real: another partner's promise clears a stale mark");
}

void testNothingToDecideFrom() {
    GameObjects objects;
    objects.mark() = markSet;
    check(!startVisit(objects, partnerReal).written && objects.mark() == markSet, "no promise (-1): nothing is written");
    objects.promise(missingDefinition);
    check(!startVisit(objects, partnerReal).written && objects.mark() == markSet,
        "a promise without a definition: nothing is written");
    check(!updateLauraMark(partnerReal, nullptr, objects.gameState.data(), objects.definitionRoot.data()).written,
        "no night visit manager: nothing is written");
    check(!updateLauraMark(partnerReal, objects.manager.data(), nullptr, objects.definitionRoot.data()).written &&
              objects.mark() == markSet,
        "no game state: nothing is written");
    objects.promise(lauraDefinition);
    check(!startVisit(objects, 7).written && objects.mark() == markSet, "an unknown setting keeps the game's value");
}

}  // namespace

int main() {
    testDefinitionLookup();
    testAsTheGameDecides();
    testFake();
    testReal();
    testNothingToDecideFrom();
    std::printf("%d checks passed, %d failed. Synthetic data only; no game installation, saves, "
                "or game process were written.\n",
        passed, failed);
    return failed ? 1 : 0;
}
