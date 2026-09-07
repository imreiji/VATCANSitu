// Tests for TagCallsign.h.
//
//   cl /EHsc /W4 /std:c++17 tests\TagCallsignTests.cpp /Fe:TagCallsignTests.exe
//   TagCallsignTests.exe

#include "../TagCallsign.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition) { ++g_failures; std::cout << "FAIL: " << what << "\n"; }
    }
}

int main()
{
    using namespace SituTag;

    // --- Showing the transponder code on a correlated ADS-B tag.
    {
        // Disagrees with the assignment -> show it. This is the case the feature exists
        // for: ADS-B correlation does not care about the code, so it can be wrong.
        Check(ShowsSquawkOnTag(true, "1200", "4321"), "code differs from assignment");
        Check(ShowsSquawkOnTag(true, "4321", "4322"), "one digit out still differs");

        // No assignment to agree with.
        Check(ShowsSquawkOnTag(true, "4321", ""), "no assigned code at all");
        Check(ShowsSquawkOnTag(true, "4321", "0000"), "0000 is not an assignment");

        // Agrees -> nothing to say, so nothing is drawn.
        Check(!ShowsSquawkOnTag(true, "4321", "4321"), "code matches the assignment");
        // 0000 means "unset" on the assignment side only. A target squawking it is
        // squawking something, and that something is worth showing.
        Check(ShowsSquawkOnTag(true, "0000", "0000"), "squawking 0000 with nothing assigned");
        Check(ShowsSquawkOnTag(true, "0000", "4321"), "squawking 0000 against an assignment");

        // Not ADS-B -> never. A conventional correlated target squawks its assignment by
        // definition of how it got correlated, and the old behaviour stands.
        Check(!ShowsSquawkOnTag(false, "1200", "4321"), "not ADS-B, not shown");
        Check(!ShowsSquawkOnTag(false, "4321", ""), "not ADS-B even with no assignment");

        // Nothing to show.
        Check(!ShowsSquawkOnTag(true, "", "4321"), "no transponder code to print");
        Check(!ShowsSquawkOnTag(true, "", ""), "nothing either side");
    }

    // --- The VF jurisdiction marker. A truth table rather than examples, because the
    //     rule has four inputs and getting one wrong writes over a controller's id.
    {
        // VFR flight plan, nobody tracking, field empty -> VF.
        Check(ShowsVfrJurisdiction(true, "4321", "", ""), "VFR flight plan and unowned");

        // Squawking 1200 counts even without a VFR flight plan.
        Check(ShowsVfrJurisdiction(false, "1200", "", ""), "1200 and unowned");
        Check(ShowsVfrJurisdiction(true, "1200", "", ""), "both");

        // IFR on a discrete code is not VFR.
        Check(!ShowsVfrJurisdiction(false, "4321", "", ""), "IFR on a discrete is not VFR");
        Check(!ShowsVfrJurisdiction(false, "", "", ""), "no squawk at all is not VFR");

        // Somebody is tracking it, so it is owned - never labelled unowned.
        Check(!ShowsVfrJurisdiction(true, "1200", "QM", ""), "tracked by another controller");
        Check(!ShowsVfrJurisdiction(true, "1200", "ZZ", ""), "tracked by anyone at all");

        // The field already has something in it. A real position id always wins; this
        // rule only fills a blank.
        Check(!ShowsVfrJurisdiction(true, "1200", "", "QM"), "never overwrites a handoff id");
        Check(!ShowsVfrJurisdiction(true, "1200", "", "ZZ"), "nor any other id");

        // Both guards at once, which is the case that would be worst to get wrong.
        Check(!ShowsVfrJurisdiction(true, "1200", "QM", "ZZ"), "tracked and with an id shown");

        // 1200 is matched exactly. A code that merely resembles it is not the
        // conspicuity code.
        Check(!ShowsVfrJurisdiction(false, "12000", "", ""), "12000 is not 1200");
        Check(!ShowsVfrJurisdiction(false, "0120", "", ""), "nor is 0120");
        Check(!ShowsVfrJurisdiction(false, "1201", "", ""), "nor is 1201");
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
