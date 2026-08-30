#pragma once

// How a callsign is written on a data tag.
//
// Canadian civil registrations are C-Gxxx, C-Fxxx and C-Ixxx. They reach the plugin from
// the network with the dash stripped - "CGABC" - and controllers say and write them
// without the nationality letter, because on a Canadian scope every one of them starts
// with the same C. So the tag drops it: CGABC is drawn GABC.
//
// The rule is deliberately narrow. Exactly five characters, a leading C, a second letter
// of G, F or I, and three letters after it. Anything else is drawn untouched, because
// the cost of a false positive is a tag showing a callsign that is not the aircraft's.
// "CGABC" shortens; "CGAB1" does not, and neither does an airline callsign that happens
// to begin with those letters.
//
// This applies only where the aircraft is correlated and the callsign came from a flight
// plan. An uncorrelated ADS-B target is drawn with its full callsign - see the tag code -
// because there the identity is what the aircraft itself is broadcasting and nothing has
// confirmed it.
//
// Depends only on the standard library. See tests/TagCallsignTests.cpp.

#include <string>

namespace SituTag
{
    inline bool IsUpperLetter(char c)
    {
        return c >= 'A' && c <= 'Z';
    }

    // C-Gxxx, C-Fxxx, C-Ixxx with the dash already stripped.
    inline bool IsCanadianRegistration(const std::string& callsign)
    {
        if (callsign.size() != 5) { return false; }
        if (callsign[0] != 'C') { return false; }
        if (callsign[1] != 'G' && callsign[1] != 'F' && callsign[1] != 'I') { return false; }

        return IsUpperLetter(callsign[2])
            && IsUpperLetter(callsign[3])
            && IsUpperLetter(callsign[4]);
    }

    // What goes on the tag. Everything that is not a Canadian registration is returned
    // unchanged, including the empty string.
    inline std::string DisplayCallsign(const std::string& callsign)
    {
        if (!IsCanadianRegistration(callsign)) { return callsign; }
        return callsign.substr(1);
    }
}
