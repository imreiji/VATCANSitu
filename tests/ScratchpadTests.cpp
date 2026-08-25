// Tests for Scratchpad.h.
//
// The scratchpad is a wire format shared with every other controller on the network, so
// the risk in replacing the hand written parsing is not "does the new code work" but
// "does the new code produce the same bytes as the old code". These tests answer that
// directly: the original logic from CSiTRadar::ModifySFI and
// CSiTRadar::ModifyCtrlRemarks is reproduced verbatim below, and every case in the
// corpus is run through both implementations and compared.
//
// A divergence is only allowed when it is one of:
//
//   1. Formatting only - the two results parse to the same SFI and remarks. This covers
//      the old code emitting a trailing space (" X ") where the new code emits the
//      canonical " X ". Both parse identically, under the old parser as well as the new.
//
//   2. The old code destroyed data - it produced an empty string from a non-empty input.
//      This is the bug being fixed.
//
// Anything else fails the run.
//
// This file is deliberately not part of VATCANSitu.vcxproj: it is a standalone program
// and Scratchpad.h depends on nothing but <string>, so it builds with a bare compiler
// and no MFC or EuroScope SDK.
//
//   cl /EHsc /W4 /std:c++17 tests\ScratchpadTests.cpp /Fe:ScratchpadTests.exe
//   ScratchpadTests.exe

#include "../Scratchpad.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    std::string Show(const std::string& s)
    {
        return "\"" + s + "\"";
    }

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::cout << "  FAIL: " << what << "\n";
        }
    }

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "  FAIL: " << what
                << " expected " << Show(expected)
                << " got " << Show(actual) << "\n";
        }
    }

    // ---------------------------------------------------------------------------------
    // The original implementations, copied from CSiTRadar.h with only the EuroScope calls
    // removed. Do not tidy these up - their value is being an exact record of the old
    // behaviour.
    // ---------------------------------------------------------------------------------

    std::string OldSetSfi(std::string scratchpad, const std::string& c)
    {
        std::string newstring;
        if (!scratchpad.empty()) {
            if (scratchpad.size() == 1) {
                newstring = " " + c + " " + scratchpad;
            }
            else if (scratchpad.size() == 2 && scratchpad.at(0) == ' ') {
                newstring = scratchpad.replace(1, 1, c);
            }
            else if (scratchpad.size() > 2) {
                if (scratchpad.at(0) == ' ' && scratchpad.at(2) == ' ') {
                    newstring = scratchpad.replace(1, 1, c);
                }
                else {
                    newstring = " " + c + " " + scratchpad;
                }
            }
        }
        else {
            newstring = " " + c;
        }
        return newstring;
    }

    std::string OldClearSfi(std::string scratchpad)
    {
        std::string newstring;
        if (scratchpad.size() == 1) {
            newstring = "";
        }
        else if (scratchpad.size() == 2 && scratchpad.at(0) == ' ') {
            newstring = "";
        }
        else if (scratchpad.size() > 2) {
            if (scratchpad.at(0) == ' ' && scratchpad.at(2) == ' ') {
                newstring = scratchpad.substr(3);
            }
            else {
                newstring = scratchpad;
            }
        }
        return newstring;
    }

    std::string OldSetRemarks(std::string scratchpad, const std::string& c)
    {
        std::string newstring;
        if (!scratchpad.empty()) {
            if (scratchpad.size() == 1) {
                newstring = c;
            }
            else if (scratchpad.size() == 2 && scratchpad.at(0) == ' ') {
                newstring = scratchpad + " " + c;
            }
            else if (scratchpad.size() > 2) {
                if (scratchpad.at(0) == ' ' && scratchpad.at(2) == ' ') {
                    newstring = scratchpad.substr(0, 3) + c;
                }
                else {
                    newstring = c;
                }
            }
        }
        else {
            newstring = c;
        }
        return newstring;
    }

    // ---------------------------------------------------------------------------------

    const std::vector<std::string>& Corpus()
    {
        static const std::vector<std::string> corpus = {
            "",
            "A", " ", "9",
            "AB", " X", "  ", "A ", " 1",
            "ABC", " X ", " XY", "  X", "A B", "   ",
            " X R",
            " X REMARKS",
            "REMARKS",
            " X NEW PILOT",
            "NEW PILOT",
            " A B C",
            // The IFR release feature writes these into the same field.
            "RREQ",
            "RREQ ABC",
            "RREL SOMETHING",
        };
        return corpus;
    }

    bool SameMeaning(const std::string& a, const std::string& b)
    {
        const Scratchpad pa = ParseScratchpad(a);
        const Scratchpad pb = ParseScratchpad(b);
        return pa.sfi == pb.sfi && pa.remarks == pb.remarks;
    }

    // Compares one old/new pair and reports whether the divergence is permitted.
    void CompareAgainstOld(const std::string& op,
        const std::string& input,
        const std::string& oldResult,
        const std::string& newResult,
        int& formattingOnly,
        int& dataLossFixed)
    {
        if (oldResult == newResult)
        {
            return;
        }

        if (SameMeaning(oldResult, newResult))
        {
            ++formattingOnly;
            return;
        }

        // The only other acceptable difference is the old code emitting nothing at all
        // for an input that had content.
        const bool oldDestroyedData = oldResult.empty() && !newResult.empty();
        Check(oldDestroyedData,
            op + " on " + Show(input) + ": old " + Show(oldResult)
            + " vs new " + Show(newResult) + " differ in meaning and old did not lose data");

        if (oldDestroyedData)
        {
            ++dataLossFixed;
        }
    }
}

int main()
{
    std::cout << "Scratchpad tests\n\n";

    // -- Encoding, stated explicitly so the wire format is pinned down by tests ---------

    std::cout << "encoding\n";
    {
        Scratchpad p = ParseScratchpad("");
        Check(p.sfi == '\0' && p.remarks.empty(), "empty parses to nothing");

        p = ParseScratchpad(" X");
        Check(p.sfi == 'X' && p.remarks.empty(), "\" X\" is an SFI with no remarks");

        p = ParseScratchpad(" X REMARKS");
        Check(p.sfi == 'X' && p.remarks == "REMARKS", "\" X REMARKS\" splits into SFI and remarks");

        p = ParseScratchpad("REMARKS");
        Check(p.sfi == '\0' && p.remarks == "REMARKS", "a bare string is all remarks");

        p = ParseScratchpad("AB");
        Check(p.sfi == '\0' && p.remarks == "AB", "two characters not starting with a space are remarks");

        p = ParseScratchpad(" X NEW PILOT");
        Check(p.sfi == 'X' && p.remarks == "NEW PILOT", "remarks may contain spaces");

        CheckEqual(FormatScratchpad({ '\0', "" }), "", "format of nothing");
        CheckEqual(FormatScratchpad({ 'X', "" }), " X", "format of SFI only");
        CheckEqual(FormatScratchpad({ 'X', "R" }), " X R", "format of SFI and remarks");
        CheckEqual(FormatScratchpad({ '\0', "R" }), "R", "format of remarks only");
    }

    // -- Parsing is stable -------------------------------------------------------------

    std::cout << "stability\n";
    for (const std::string& raw : Corpus())
    {
        const Scratchpad once = ParseScratchpad(raw);
        const Scratchpad twice = ParseScratchpad(FormatScratchpad(once));

        Check(once.sfi == twice.sfi && once.remarks == twice.remarks,
            "parse of " + Show(raw) + " is stable through a format round trip");
    }

    // -- The bug this change exists to fix ---------------------------------------------

    std::cout << "regressions\n";
    {
        // A two character remark whose first character is not a space matched no branch
        // in the old code, so newstring stayed empty and was written back regardless.
        CheckEqual(OldSetSfi("AB", "X"), "", "old code lost a two character remark (recorded, not desired)");
        CheckEqual(ScratchpadWithSfi("AB", 'X'), " X AB", "setting an SFI keeps a two character remark");

        CheckEqual(OldSetRemarks("AB", "NEW"), "", "old code lost remarks when replacing them (recorded, not desired)");
        CheckEqual(ScratchpadWithRemarks("AB", "NEW"), "NEW", "replacing remarks works on a two character scratchpad");

        // Clearing the SFI of a one character remark also dropped the remark.
        CheckEqual(OldClearSfi("A"), "", "old code lost a one character remark on clear (recorded, not desired)");
        CheckEqual(ScratchpadWithoutSfi("A"), "A", "clearing a non existent SFI keeps the remark");
    }

    // -- Ordinary edits ----------------------------------------------------------------

    std::cout << "edits\n";
    {
        CheckEqual(ScratchpadWithSfi("", 'X'), " X", "set SFI on empty");
        CheckEqual(ScratchpadWithSfi("REMARKS", 'X'), " X REMARKS", "set SFI keeping remarks");
        CheckEqual(ScratchpadWithSfi(" A REMARKS", 'X'), " X REMARKS", "replace an existing SFI");
        CheckEqual(ScratchpadWithSfi(" A", 'X'), " X", "replace an SFI with no remarks");

        CheckEqual(ScratchpadWithoutSfi(" X REMARKS"), "REMARKS", "clear SFI keeping remarks");
        CheckEqual(ScratchpadWithoutSfi(" X"), "", "clear SFI with no remarks");
        CheckEqual(ScratchpadWithoutSfi("REMARKS"), "REMARKS", "clear SFI when there is none");

        CheckEqual(ScratchpadWithRemarks(" X OLD", "NEW"), " X NEW", "replace remarks keeping SFI");
        CheckEqual(ScratchpadWithRemarks(" X OLD", ""), " X", "blank remarks keeping SFI");
        CheckEqual(ScratchpadWithRemarks("", "NEW"), "NEW", "set remarks with no SFI");
        CheckEqual(ScratchpadWithRemarks("NEW PILOT", ""), "", "blank remarks with no SFI");
    }

    // -- Exhaustive comparison against the old implementation --------------------------

    std::cout << "compatibility with the old implementation\n";
    int formattingOnly = 0;
    int dataLossFixed = 0;

    const std::vector<std::string> sfis = { "A", "Z", "9" };
    const std::vector<std::string> remarkValues = { "", "R", "NEW PILOT" };

    for (const std::string& raw : Corpus())
    {
        for (const std::string& sfi : sfis)
        {
            CompareAgainstOld("set SFI " + sfi, raw,
                OldSetSfi(raw, sfi),
                ScratchpadWithSfi(raw, sfi[0]),
                formattingOnly, dataLossFixed);
        }

        CompareAgainstOld("clear SFI", raw,
            OldClearSfi(raw),
            ScratchpadWithoutSfi(raw),
            formattingOnly, dataLossFixed);

        for (const std::string& remarks : remarkValues)
        {
            CompareAgainstOld("set remarks " + Show(remarks), raw,
                OldSetRemarks(raw, remarks),
                ScratchpadWithRemarks(raw, remarks),
                formattingOnly, dataLossFixed);
        }
    }

    std::cout << "  " << formattingOnly << " formatting only differences\n";
    std::cout << "  " << dataLossFixed << " cases where the old code destroyed data\n";

    Check(dataLossFixed > 0, "the corpus actually exercises the data loss bug");

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
