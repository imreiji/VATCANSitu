// Tests for CpdlcUplinks.h.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcUplinkTests.cpp /Fe:CpdlcUplinkTests.exe
//   CpdlcUplinkTests.exe
//
// The three named bugs in the header are each pinned by a check below, so a future port
// from upstream cannot quietly reintroduce them.

#include "../CpdlcUplinks.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected \"" << expected
                      << "\", got \"" << actual << "\"\n";
        }
    }

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << "\n";
        }
    }
}

int main()
{
    using namespace SituCpdlcUplinks;

    // --- Bug 1. Upstream computed the below-transition altitude into a local and never
    //     appended it, so every clearance under FL180 went out with an empty field. A
    //     climb to 8000 must carry 8000.
    CheckEqual(ClimbTo(8000), "CLIMB TO AND MAINTAIN @8000@", "a low level is in the message");
    CheckEqual(DescendTo(6000), "DESCEND TO AND MAINTAIN @6000@", "and so is a low descent");
    Check(ClimbTo(8000).find("@@") == std::string::npos, "no empty variable field");

    // --- Bug 2. 1 and 2 are the approach-clearance markers, not altitudes.
    Check(!IsAltitudeClearance(0), "0 is no clearance");
    Check(!IsAltitudeClearance(1), "1 is cleared ILS, not one foot");
    Check(!IsAltitudeClearance(2), "2 is cleared visual");
    Check(IsAltitudeClearance(3000), "a real altitude is a clearance");
    CheckEqual(FormatAltitude(1), "1", "formatting alone does not judge - the caller filters");
    CheckEqual(std::to_string(LevelToUplink(1, 35000)), "35000", "cleared-ILS falls through to the final altitude");
    CheckEqual(std::to_string(LevelToUplink(2, 35000)), "35000", "so does cleared-visual");
    CheckEqual(std::to_string(LevelToUplink(0, 35000)), "35000", "and so does no clearance at all");
    CheckEqual(std::to_string(LevelToUplink(11000, 35000)), "11000", "a clearance wins over the filed level");

    // --- Bug 3. Mach comes back multiplied by 100, from both sources.
    CheckEqual(FormatMach(82), "M0.82", "M0.82");
    CheckEqual(FormatMach(80), "M0.80", "a trailing zero is kept");
    CheckEqual(FormatMach(8), "M0.08", "a single digit is padded, not shifted");
    CheckEqual(FormatMach(100), "M1.00", "Mach 1");
    CheckEqual(FormatMach(203), "M2.03", "supersonic still formats");
    CheckEqual(FormatMach(0), "", "no assigned Mach yields nothing");
    CheckEqual(FormatMach(-5), "", "and neither does a negative");
    CheckEqual(MaintainMach(82, Exactly), "MAINTAIN @M0.82@", "the Mach message");

    // --- Altitude formatting either side of the transition.
    CheckEqual(FormatAltitude(17999), "17999", "just below the transition is feet");
    CheckEqual(FormatAltitude(18000), "FL180", "the transition itself is a level");
    CheckEqual(FormatAltitude(35000), "FL350", "and above it");
    CheckEqual(FormatAltitude(0), "", "no altitude yields nothing");
    CheckEqual(FormatAltitude(-100), "", "nor a negative one");
    CheckEqual(FormatAltitude(35050), "FL350", "an off-level value rounds down");

    // --- An empty value must produce no message at all, rather than an instruction the
    //     aircraft cannot comply with.
    CheckEqual(ClimbTo(0), "", "no altitude, no climb message");
    CheckEqual(DescendTo(0), "", "no altitude, no descent message");
    CheckEqual(MaintainSpeed(0, Exactly), "", "no speed, no speed message");
    CheckEqual(MaintainMach(0, OrLess), "", "no Mach, no Mach message");
    CheckEqual(ProceedDirect(""), "", "no fix, no direct message");

    // --- Speed and its qualifiers. The qualifier sits outside the variable field.
    CheckEqual(MaintainSpeed(250, Exactly), "MAINTAIN @250KTS@", "plain speed");
    CheckEqual(MaintainSpeed(250, OrGreater), "MAINTAIN @250KTS@ OR GREATER", "or greater");
    CheckEqual(MaintainSpeed(250, OrLess), "MAINTAIN @250KTS@ OR LESS", "or less");
    CheckEqual(MaintainMach(82, OrGreater), "MAINTAIN @M0.82@ OR GREATER", "Mach or greater");
    CheckEqual(MaintainMach(82, OrLess), "MAINTAIN @M0.82@ OR LESS", "Mach or less");

    {
        const std::string speed = MaintainSpeed(250, OrGreater);
        const size_t close = speed.find_last_of('@');
        Check(close != std::string::npos && speed.substr(close + 1) == " OR GREATER",
              "the qualifier is outside the field, so the value can be edited alone");
    }

    // --- Direct-to.
    CheckEqual(ProceedDirect("YYT"), "PROCEED DIRECT @YYT@", "a fix name");
    CheckEqual(ProceedDirect("5100N05000W"), "PROCEED DIRECT @5100N05000W@", "an oceanic point");

    // --- Reply types travel with the wording.
    CheckEqual(ReplyTypeFor(ConfirmAssignedAltitude()), "NE", "a confirmation expects no reply");
    CheckEqual(ReplyTypeFor(SurveillanceTerminated()), "R", "surveillance terminated wants a roger");
    CheckEqual(ReplyTypeFor(ClimbTo(35000)), "WU", "a clearance wants wilco or unable");

    // --- Every composed message either is empty or has exactly two delimiters, so the
    //     editor can always find the value. This is the invariant the whole '@' scheme
    //     rests on, checked over the full catalogue rather than one example.
    {
        const std::string composed[] = {
            ClimbTo(35000), ClimbTo(8000), DescendTo(11000),
            MaintainSpeed(250, Exactly), MaintainSpeed(300, OrGreater), MaintainSpeed(280, OrLess),
            MaintainMach(82, Exactly), MaintainMach(78, OrLess),
            ProceedDirect("YQX"),
        };

        bool allPaired = true;
        for (const std::string& message : composed)
        {
            size_t delimiters = 0;
            for (char c : message) { if (c == '@') { ++delimiters; } }
            if (delimiters != 2) { allPaired = false; std::cout << "   unpaired: " << message << "\n"; }
        }
        Check(allPaired, "every value message has exactly one variable field");

        // And the fixed ones have none at all.
        Check(ConfirmAssignedAltitude().find('@') == std::string::npos, "a fixed message has no field");
        Check(SurveillanceTerminated().find('@') == std::string::npos, "nor does the other");
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
