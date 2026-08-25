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

struct Scratchpad
{
    // '\0' when no SFI is set.
    char sfi{ '\0' };
    std::string remarks;
};

inline Scratchpad ParseScratchpad(const std::string& raw)
{
    Scratchpad parsed;

    // " X" - SFI with no remarks.
    if (raw.size() == 2 && raw[0] == ' ')
    {
        parsed.sfi = raw[1];
        return parsed;
    }

    // " X REMARKS" - SFI followed by remarks.
    if (raw.size() > 2 && raw[0] == ' ' && raw[2] == ' ')
    {
        parsed.sfi = raw[1];
        parsed.remarks = raw.substr(3);
        return parsed;
    }

    // Anything else is remarks, including the empty string and any single character.
    parsed.remarks = raw;
    return parsed;
}

inline std::string FormatScratchpad(const Scratchpad& value)
{
    if (value.sfi == '\0')
    {
        return value.remarks;
    }

    if (value.remarks.empty())
    {
        return std::string(" ") + value.sfi;
    }

    return std::string(" ") + value.sfi + " " + value.remarks;
}

// Convenience wrappers matching the three edits the UI actually performs.

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
