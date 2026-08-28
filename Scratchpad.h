#pragma once

#include <string>

// Parsing and formatting for the EuroScope scratchpad string.
//
// The plugin stores two of its own fields in the scratchpad: a single character SFI
// (the letter shown after the callsign on line 1 of the tag) and free form controller
// remarks (shown on the last line). The scratchpad is synchronised to every other
// controller on the network, so its layout is a WIRE FORMAT. Other people's EuroScope,
// other plugins and other controllers' eyes read it. The encoding below must not change.
//
//   ""            no SFI, no remarks
//   "REMARKS"     remarks only, no SFI
//   " X"          SFI only
//   " X REMARKS"  SFI and remarks
//
// So an SFI is present exactly when the string starts with a space, has at least two
// characters, and is either exactly two characters long or has a space at index 2.
// Everything else is remarks.
//
// This layout is inherently ambiguous: remarks that themselves begin with " X " parse
// back as an SFI plus shorter remarks. That ambiguity is a property of the existing
// format, not of this code, and is left alone deliberately - resolving it would mean
// changing what other controllers see.
//
// These functions replace hand written parsing that was open coded in four places
// (CSiTRadar::ModifySFI, CSiTRadar::ModifyCtrlRemarks, and twice in ACTag.cpp for
// drawing). Each was a chain of scratchpad.size() and scratchpad.at() tests, and the two
// in CSiTRadar.h shared a defect: a two character scratchpad whose first character was
// not a space matched no branch, left the output string empty, and was then written back
// unconditionally - silently destroying the controller's remarks.

// IFR release coordination state, stored as a prefix on the same string.
//
//   "RREQ ..."  a tower or ground controller has requested release
//   "RREL ..."  the owning approach or centre controller has granted it
//
// This is a third writer of the same field. It was added without knowledge of the SFI
// layout above, so it simply prepended "RREQ " to whatever was already there and, when
// toggled off, blanked the entire string before restoring part of it.
//
// The two features could not both work: SFI detection requires a leading space, and a
// requested release put "RREQ " in front of it, so the SFI stopped parsing for any
// aircraft with both set. That is a property of the code as written rather than an
// incident anybody reported - it was deduced from reading these two paths, and no
// controller report of it is on record. Stated that way round deliberately: the earlier
// wording here said the features "corrupted each other" in the past tense, which claims
// an observation this file never had. The code made it certain for anyone who used both;
// whether anyone did is unknown.
//
// The prefix is kept exactly as it is on the wire. What changes is that it is now parsed
// back out before the SFI and remarks are read, which is what the existing byte layout
// always implied - "RREQ " + " X REMARKS" is already how the old code wrote it, it just
// never read it that way.
enum class ReleaseState
{
    None,
    Requested,
    Granted
};

struct Scratchpad
{
    ReleaseState release{ ReleaseState::None };

    // '\0' when no SFI is set.
    char sfi{ '\0' };
    std::string remarks;
};

namespace ScratchpadDetail
{
    // Matches a release keyword only when it stands alone or is followed by a space, so
    // remarks such as "RREQUEST FUEL" are not mistaken for a release marker.
    inline bool TakeReleasePrefix(const std::string& raw, const char* keyword, std::string& rest)
    {
        const std::string key(keyword);

        if (raw.compare(0, key.size(), key) != 0)
        {
            return false;
        }

        if (raw.size() == key.size())
        {
            rest.clear();
            return true;
        }

        if (raw[key.size()] != ' ')
        {
            return false;
        }

        rest = raw.substr(key.size() + 1);
        return true;
    }

    inline void ParseSfiAndRemarks(const std::string& raw, Scratchpad& parsed)
    {
        // " X" - SFI with no remarks.
        if (raw.size() == 2 && raw[0] == ' ')
        {
            parsed.sfi = raw[1];
            return;
        }

        // " X REMARKS" - SFI followed by remarks.
        if (raw.size() > 2 && raw[0] == ' ' && raw[2] == ' ')
        {
            parsed.sfi = raw[1];
            parsed.remarks = raw.substr(3);
            return;
        }

        // Anything else is remarks, including the empty string and any single character.
        parsed.remarks = raw;
    }
}

inline Scratchpad ParseScratchpad(const std::string& raw)
{
    Scratchpad parsed;

    std::string rest;
    if (ScratchpadDetail::TakeReleasePrefix(raw, "RREQ", rest))
    {
        parsed.release = ReleaseState::Requested;
    }
    else if (ScratchpadDetail::TakeReleasePrefix(raw, "RREL", rest))
    {
        parsed.release = ReleaseState::Granted;
    }
    else
    {
        rest = raw;
    }

    ScratchpadDetail::ParseSfiAndRemarks(rest, parsed);
    return parsed;
}

inline std::string FormatScratchpad(const Scratchpad& value)
{
    std::string body;

    if (value.sfi == '\0')
    {
        body = value.remarks;
    }
    else if (value.remarks.empty())
    {
        body = std::string(" ") + value.sfi;
    }
    else
    {
        body = std::string(" ") + value.sfi + " " + value.remarks;
    }

    if (value.release == ReleaseState::None)
    {
        return body;
    }

    const std::string keyword = (value.release == ReleaseState::Requested) ? "RREQ" : "RREL";

    // No trailing space when there is nothing after the keyword, matching what the
    // original code wrote for a bare grant.
    return body.empty() ? keyword : keyword + " " + body;
}

// Convenience wrappers matching the edits the UI actually performs. Each parses, changes
// one field and reformats, so every other field survives - which is the whole point:
// setting an SFI no longer discards a release request, and vice versa.

inline std::string ScratchpadWithRelease(const std::string& raw, ReleaseState release)
{
    Scratchpad value = ParseScratchpad(raw);
    value.release = release;
    return FormatScratchpad(value);
}

inline std::string ScratchpadWithSfi(const std::string& raw, char sfi)
{
    Scratchpad value = ParseScratchpad(raw);
    value.sfi = sfi;
    return FormatScratchpad(value);
}

inline std::string ScratchpadWithoutSfi(const std::string& raw)
{
    Scratchpad value = ParseScratchpad(raw);
    value.sfi = '\0';
    return FormatScratchpad(value);
}

inline std::string ScratchpadWithRemarks(const std::string& raw, const std::string& remarks)
{
    Scratchpad value = ParseScratchpad(raw);
    value.remarks = remarks;
    return FormatScratchpad(value);
}
