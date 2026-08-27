#pragma once

// Composing the CPDLC uplinks that carry a value - an altitude, a speed, a Mach number,
// a fix - from the values EuroScope already holds for the aircraft.
//
// The wording is the upstream cpdlc branch's, verbatim, because that is what aircraft
// clients have been parsing. What changed is that the composition lives here, below the
// SDK boundary, where a test can reach it. Everything above passes plain ints and
// strings; nothing in this file includes EuroScopePlugIn.h.
//
// '@' is Hoppie's delimiter for a variable field. Aircraft display the message with the
// delimiters stripped, and controllers see the same - see RenderTextField. Keeping them
// in the composed string is what lets the editor find the value again later.
//
// Three things upstream gets wrong that the tests here pin down:
//
//   1. An altitude below the transition was computed into a local and never appended, so
//      every clearance under FL180 went out as "CLIMB TO AND MAINTAIN @" with no value.
//   2. GetClearedAltitude returns 1 for cleared-ILS and 2 for cleared-visual, not an
//      altitude. Testing it for non-zero turned those into "@FL0@".
//   3. PerformanceGetMach returns Mach x100 as an int, but the fallback path fed a
//      double through to_string, producing "M0.0.820000".
//
// See tests/CpdlcUplinkTests.cpp.

#include <string>

namespace SituCpdlcUplinks
{
    // Canadian domestic transition altitude. Below it a clearance is spoken in feet,
    // at or above it as a flight level.
    const int kTransitionAltitude = 18000;

    // GetClearedAltitude's documented special values: 0 is "no clearance, use the final
    // altitude instead", 1 is cleared for ILS, 2 is cleared for visual. None of them is
    // a height, and only a height can go into a level clearance.
    inline bool IsAltitudeClearance(int clearedAltitude)
    {
        return clearedAltitude > 2;
    }

    // "FL350" at or above the transition, plain feet below it. Zero or negative yields
    // an empty string, which callers treat as "no value to send".
    inline std::string FormatAltitude(int feet)
    {
        if (feet <= 0) { return std::string(); }
        if (feet < kTransitionAltitude) { return std::to_string(feet); }

        // Round down to the flight level. A clearance is always issued on a level, so a
        // value that is not a multiple of 100 came from somewhere untrustworthy; taking
        // the level below is the conservative reading of it.
        return "FL" + std::to_string(feet / 100);
    }

    // Mach x100, as both GetAssignedMach and PerformanceGetMach return it. 82 is M0.82.
    // Values at or above 100 are supersonic and formatted whole rather than refused.
    inline std::string FormatMach(int machTimes100)
    {
        if (machTimes100 <= 0) { return std::string(); }

        const int whole = machTimes100 / 100;
        const int fraction = machTimes100 % 100;

        std::string out = "M" + std::to_string(whole) + ".";
        if (fraction < 10) { out += "0"; }
        out += std::to_string(fraction);
        return out;
    }

    enum Qualifier
    {
        Exactly,
        OrGreater,
        OrLess,
    };

    // The qualifier sits outside the variable field, so editing the value later does not
    // have to know the phrase it was wrapped in.
    inline std::string QualifierSuffix(Qualifier qualifier)
    {
        if (qualifier == OrGreater) { return " OR GREATER"; }
        if (qualifier == OrLess)    { return " OR LESS"; }
        return std::string();
    }

    // Each of these returns empty when it has no value to put in the field. An uplink
    // with an empty variable field is worse than no uplink: the aircraft receives an
    // instruction with nothing to comply with.

    inline std::string ClimbTo(int feet)
    {
        const std::string level = FormatAltitude(feet);
        if (level.empty()) { return std::string(); }
        return "CLIMB TO AND MAINTAIN @" + level + "@";
    }

    inline std::string DescendTo(int feet)
    {
        const std::string level = FormatAltitude(feet);
        if (level.empty()) { return std::string(); }
        return "DESCEND TO AND MAINTAIN @" + level + "@";
    }

    inline std::string MaintainSpeed(int knots, Qualifier qualifier)
    {
        if (knots <= 0) { return std::string(); }
        return "MAINTAIN @" + std::to_string(knots) + "KTS@" + QualifierSuffix(qualifier);
    }

    inline std::string MaintainMach(int machTimes100, Qualifier qualifier)
    {
        const std::string mach = FormatMach(machTimes100);
        if (mach.empty()) { return std::string(); }
        return "MAINTAIN @" + mach + "@" + QualifierSuffix(qualifier);
    }

    inline std::string ProceedDirect(const std::string& fix)
    {
        if (fix.empty()) { return std::string(); }
        return "PROCEED DIRECT @" + fix + "@";
    }

    // Fixed wording, kept here so the whole uplink catalogue reads in one place.
    inline std::string ConfirmAssignedAltitude() { return "CONFIRM ASSIGNED ALTITUDE"; }
    inline std::string SurveillanceTerminated()  { return "SURVEILLANCE SERVICES TERMINATED"; }

    // What the aircraft is asked for in reply, per message. Kept beside the wording so
    // the two cannot drift apart.
    inline std::string ReplyTypeFor(const std::string& message)
    {
        if (message == ConfirmAssignedAltitude()) { return "NE"; }
        if (message == SurveillanceTerminated())  { return "R"; }
        return "WU";
    }

    // The value a level clearance should carry: what the controller has cleared the
    // aircraft to, or the filed final altitude when nothing has been cleared.
    inline int LevelToUplink(int clearedAltitude, int finalAltitude)
    {
        return IsAltitudeClearance(clearedAltitude) ? clearedAltitude : finalAltitude;
    }
}
