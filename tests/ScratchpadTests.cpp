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

    // The original IFR release logic from SituPlugin::OnFunctionCall, reduced to its net
    // effect on the string. The original wrote the field twice on the clearing paths;
    // only the final value is modelled here.

    std::string OldToggleRequest(const std::string& sp)
    {
        if (sp.compare(0, 4, "RREQ") == 0) {
            return sp.size() > 4 ? sp.substr(5) : "";
        }
        if (sp.compare(0, 4, "RREL") == 0) {
            return sp.size() > 4 ? sp.substr(5) : "";
        }
        return "RREQ " + sp;
    }

    std::string OldGrantRelease(const std::string& sp)
    {
        if (sp.compare(0, 4, "RREQ") == 0) {
            return sp.size() > 4 ? ("RREL " + sp.substr(5)) : "RREL";
        }
        return sp;   // unchanged when there is no outstanding request
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
        };
        return corpus;
    }

    // Release prefixed values are deliberately kept out of the corpus above. The old SFI
    // code had no concept of a release prefix - it treated "RREQ ABC" as remarks - so a
    // strict diff against it would report intended behaviour as a regression. Release
    // handling is covered by its own section, against explicit expectations.
    const std::vector<std::string>& ReleaseCorpus()
    {
        static const std::vector<std::string> corpus = {
            "RREQ",
            "RREQ ",
            "RREL",
            "RREQ ABC",
            "RREL SOMETHING",
            "RREQ  X ABC",
            "RREL  X NEW PILOT",
            "RREQ  X",
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

        CheckEqual(FormatScratchpad({ ReleaseState::None, '\0', "" }), "", "format of nothing");
        CheckEqual(FormatScratchpad({ ReleaseState::None, 'X', "" }), " X", "format of SFI only");
        CheckEqual(FormatScratchpad({ ReleaseState::None, 'X', "R" }), " X R", "format of SFI and remarks");
        CheckEqual(FormatScratchpad({ ReleaseState::None, '\0', "R" }), "R", "format of remarks only");
    }

    // -- Release state, which shares the same string ------------------------------------

    std::cout << "release state\n";
    {
        // Byte compatibility with what the old IFR release code wrote, so scratchpads
        // already live on the network keep their meaning.
        Check(ParseScratchpad("RREQ").release == ReleaseState::Requested, "\"RREQ\" alone is a request");
        Check(ParseScratchpad("RREQ ").release == ReleaseState::Requested, "\"RREQ \" with the trailing space it used to write");
        Check(ParseScratchpad("RREL").release == ReleaseState::Granted, "\"RREL\" alone is a grant");

        Scratchpad p = ParseScratchpad("RREQ ABC");
        Check(p.release == ReleaseState::Requested && p.sfi == '\0' && p.remarks == "ABC",
            "request plus remarks");

        // What the old code produced when an SFI was already present: "RREQ " + " X ABC".
        // It wrote this and could then never read the SFI back out of it.
        p = ParseScratchpad("RREQ  X ABC");
        Check(p.release == ReleaseState::Requested && p.sfi == 'X' && p.remarks == "ABC",
            "request, SFI and remarks all recovered from a string the old code mangled");

        p = ParseScratchpad("RREQ  X");
        Check(p.release == ReleaseState::Requested && p.sfi == 'X' && p.remarks.empty(),
            "request plus SFI, no remarks");

        // The old code used strncmp(.., "RREQ", 4), which matched any remark starting
        // with those letters.
        p = ParseScratchpad("RREQUEST FUEL");
        Check(p.release == ReleaseState::None && p.remarks == "RREQUEST FUEL",
            "a remark beginning RREQ is not a release request");

        p = ParseScratchpad("RRELAY TO CENTRE");
        Check(p.release == ReleaseState::None && p.remarks == "RRELAY TO CENTRE",
            "a remark beginning RREL is not a release grant");

        CheckEqual(FormatScratchpad({ ReleaseState::Requested, '\0', "" }), "RREQ", "bare request has no trailing space");
        CheckEqual(FormatScratchpad({ ReleaseState::Granted, '\0', "" }), "RREL", "bare grant has no trailing space");
        CheckEqual(FormatScratchpad({ ReleaseState::Requested, 'X', "ABC" }), "RREQ  X ABC", "request plus SFI plus remarks");

        // The point of the change: the three fields no longer destroy each other.
        CheckEqual(ScratchpadWithRelease(" X ABC", ReleaseState::Requested), "RREQ  X ABC",
            "requesting release keeps the SFI and remarks");
        CheckEqual(ScratchpadWithSfi("RREQ ABC", 'X'), "RREQ  X ABC",
            "setting an SFI keeps an outstanding request");
        CheckEqual(ScratchpadWithRemarks("RREQ  X OLD", "NEW"), "RREQ  X NEW",
            "editing remarks keeps both the request and the SFI");
        CheckEqual(ScratchpadWithRelease("RREQ  X ABC", ReleaseState::Granted), "RREL  X ABC",
            "granting keeps the SFI and remarks");
        CheckEqual(ScratchpadWithRelease("RREQ  X ABC", ReleaseState::None), " X ABC",
            "clearing the request leaves the SFI and remarks behind");
        CheckEqual(ScratchpadWithoutSfi("RREQ  X ABC"), "RREQ ABC",
            "clearing the SFI keeps the request");
    }

    // -- Every combination survives a round trip ---------------------------------------

    std::cout << "round trip over all field combinations\n";
    {
        const ReleaseState releases[] = { ReleaseState::None, ReleaseState::Requested, ReleaseState::Granted };
        const char sfis[] = { '\0', 'X', '9' };
        const char* remarkValues[] = { "", "R", "NEW PILOT" };

        for (ReleaseState release : releases)
        {
            for (char sfi : sfis)
            {
                for (const char* remarks : remarkValues)
                {
                    const Scratchpad original{ release, sfi, remarks };
                    const Scratchpad returned = ParseScratchpad(FormatScratchpad(original));

                    Check(returned.release == original.release
                        && returned.sfi == original.sfi
                        && returned.remarks == original.remarks,
                        "round trip of release/" + std::string(1, sfi ? sfi : '_') + "/" + Show(remarks));
                }
            }
        }
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

    // -- The release state machine is unchanged ----------------------------------------
    //
    // The bytes differ where the old code could not see an SFI inside the tail, but the
    // state transition itself must match the original exactly, or controllers would see
    // release behave differently after this change.

    std::cout << "release state machine matches the original\n";
    {
        std::vector<std::string> releaseInputs = Corpus();
        const std::vector<std::string>& withPrefix = ReleaseCorpus();
        releaseInputs.insert(releaseInputs.end(), withPrefix.begin(), withPrefix.end());

        int preservedMore = 0;

        for (const std::string& raw : releaseInputs)
        {
            // Toggle.
            const ReleaseState current = ParseScratchpad(raw).release;
            const ReleaseState next = (current == ReleaseState::None)
                ? ReleaseState::Requested
                : ReleaseState::None;

            const std::string oldToggled = OldToggleRequest(raw);
            const std::string newToggled = ScratchpadWithRelease(raw, next);

            Check(ParseScratchpad(oldToggled).release == ParseScratchpad(newToggled).release,
                "toggle of " + Show(raw) + " reaches the same release state");

            // Grant.
            const std::string oldGranted = OldGrantRelease(raw);
            const std::string newGranted =
                (ParseScratchpad(raw).release == ReleaseState::Requested)
                ? ScratchpadWithRelease(raw, ReleaseState::Granted)
                : raw;

            Check(ParseScratchpad(oldGranted).release == ParseScratchpad(newGranted).release,
                "grant on " + Show(raw) + " reaches the same release state");

            // Where the bytes differ it must be for one of two reasons: the same trailing
            // space canonicalisation as elsewhere, or the new code keeping an SFI that the
            // old one buried in the remarks. Anything else is a real behaviour change.
            if (oldToggled != newToggled)
            {
                if (SameMeaning(oldToggled, newToggled))
                {
                    ++formattingOnly;
                }
                else
                {
                    const Scratchpad oldParsed = ParseScratchpad(oldToggled);
                    const Scratchpad newParsed = ParseScratchpad(newToggled);

                    Check(newParsed.sfi != '\0' && oldParsed.sfi == '\0',
                        "toggle of " + Show(raw) + " differs only by recovering an SFI: old "
                        + Show(oldToggled) + " new " + Show(newToggled));

                    ++preservedMore;
                }
            }
        }

        std::cout << "  " << preservedMore << " cases where the new code recovered an SFI the old one lost\n";

        // That count is expected to be zero, and the reason is the whole point of this
        // change: the old WRITE path was already correct. "RREQ " + " X ABC" produces
        // exactly the layered encoding the new parser expects. What was broken was
        // reading it back - the tag item's strncmp and the SFI parser both failed on a
        // string the release code itself had written. So the bytes on the network never
        // needed to change; only our ability to interpret them did.
        //
        // Assert that directly: everything the old code could write must parse.
        for (const std::string& raw : releaseInputs)
        {
            const std::string oldWrote = OldToggleRequest(raw);
            const Scratchpad reparsed = ParseScratchpad(oldWrote);

            const bool expectRequest = (ParseScratchpad(raw).release == ReleaseState::None);

            Check(reparsed.release == (expectRequest ? ReleaseState::Requested : ReleaseState::None),
                "the new parser reads back what the old toggle wrote for " + Show(raw)
                + " -> " + Show(oldWrote));
        }

        for (const std::string& raw : ReleaseCorpus())
        {
            const Scratchpad before = ParseScratchpad(raw);
            if (before.release != ReleaseState::Requested) { continue; }

            const Scratchpad afterOldGrant = ParseScratchpad(OldGrantRelease(raw));

            Check(afterOldGrant.release == ReleaseState::Granted
                && afterOldGrant.sfi == before.sfi
                && afterOldGrant.remarks == before.remarks,
                "the new parser reads back what the old grant wrote for " + Show(raw));
        }
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
