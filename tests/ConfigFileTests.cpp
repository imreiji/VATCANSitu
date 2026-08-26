// Tests for ConfigFile.h.
//
// The corpus below is taken from the live CZQM TopSkyCPDLC.txt and from the TopSky
// developer guide's documented line forms, because the point of this parser is to read
// files controllers already have rather than a format we invented. Anything it gets
// wrong on real TopSky input is a defect even if the syntax is arguably ours.
//
// Standalone: ConfigFile.h depends only on the standard library.
//
//   cl /EHsc /W4 /std:c++17 tests\ConfigFileTests.cpp /Fe:ConfigFileTests.exe
//   ConfigFileTests.exe

#include "../ConfigFile.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << "\n";
        }
    }

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

    void CheckEqual(size_t actual, size_t expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected " << expected
                      << ", got " << actual << "\n";
        }
    }
}

int main()
{
    using namespace SituConfig;

    // --- A Key=Value line above any section, which is how the real CPDLC file opens.
    {
        const ParseResult r = Parse(
            "CPDLC_FSM_CLD_Header=<hour><min> <year2><month2><day> <adep> PDC <number> @<callsign>@\n"
            "[STATIONS]\n"
            "LOGIN:CZQM:MONCTON CTR:QM\n");

        CheckEqual(r.records.size(), 2u, "two records");
        Check(r.records[0].isAssignment, "the header line is an assignment");
        CheckEqual(r.records[0].section, "", "it sits above any section");
        CheckEqual(r.records[0].key, "CPDLC_FSM_CLD_Header", "its key");
        CheckEqual(r.records[0].value,
                   "<hour><min> <year2><month2><day> <adep> PDC <number> @<callsign>@",
                   "its value, placeholders and @ intact");

        Check(!r.records[1].isAssignment, "the LOGIN line is a colon record");
        CheckEqual(r.records[1].section, "STATIONS", "it is inside [STATIONS]");
        CheckEqual(r.records[1].type, "LOGIN", "its type");
        CheckEqual(r.records[1].fields.size(), 3u, "three fields");
        CheckEqual(r.records[1].fields[0], "CZQM", "login");
        CheckEqual(r.records[1].fields[1], "MONCTON CTR", "radio callsign, space preserved");
        CheckEqual(r.records[1].fields[2], "QM", "controller id");
    }

    // --- Comments and blank lines. TopSky uses // both for prose grouping headers and
    //     to disable a line, so a commented-out record must not come back as a record.
    {
        const ParseResult r = Parse(
            "//Canada\n"
            "\n"
            "LOGIN:CZQX:GANDER RADIO:QX\n"
            "//DCL:*:SID:an alternate format kept but disabled\n"
            "   \n"
            "//Gander Radio\n"
            "LOGIN:ZQOG:GANDER OCEANIC:GD\n");

        CheckEqual(r.records.size(), 2u, "only the two live LOGIN lines");
        CheckEqual(r.records[0].fields[0], "CZQX", "first");
        CheckEqual(r.records[1].fields[0], "ZQOG", "second");
        CheckEqual(r.skippedLines.size(), 0u, "comments and blanks are not skipped lines");
    }

    // --- An indented comment is still a comment.
    {
        const ParseResult r = Parse("    // indented\nLOGIN:A:B:C\n");
        CheckEqual(r.records.size(), 1u, "indented comment ignored");
    }

    // --- FREETEXT, including the reply types and a trailing space that must survive.
    {
        const ParseResult r = Parse(
            "[FREETEXT]\n"
            "FREETEXT:UNICOM:SERVICES TERMINATED - MNT UNICOM 122.8\n"
            "FREETEXT:NE:ROGER\n"
            "FREETEXT:R:SUBMIT REQ FOR NAT CROSSING VIA NATTRAK.VATSIM.NET\n"
            "FREETEXT:WU:CONTACT ATC ON VOICE \n");

        CheckEqual(r.records.size(), 4u, "four freetext records");
        CheckEqual(r.records[0].fields[0], "UNICOM", "UNICOM reply type");
        CheckEqual(r.records[0].fields[1], "SERVICES TERMINATED - MNT UNICOM 122.8", "its text");
        CheckEqual(r.records[2].fields[1],
                   "SUBMIT REQ FOR NAT CROSSING VIA NATTRAK.VATSIM.NET",
                   "a dotted hostname is not split");

        // Two lines in the live file carry a deliberate trailing space. Trimming it would
        // silently change the message that goes on the wire.
        CheckEqual(r.records[3].fields[1], "CONTACT ATC ON VOICE ", "trailing space preserved");
    }

    // --- DCL, where the text contains colons and placeholders and must not be split
    //     past its documented field count.
    {
        const ParseResult r = Parse(
            "[DCL]\n"
            "DCL:*:SID:CLRD TO <ades> OFF <drwy> VIA <sid> SQUAWK <assr> ; DEPARTURE FREQUENCY <freq_dep>\n"
            "DCL:CYQM,CYYZ:AHDG+SID:CLRD TO <ades>\n");

        CheckEqual(r.records[0].type, "DCL", "type");
        CheckEqual(r.records[0].fields.size(), 3u, "adep, subtype, text");
        CheckEqual(r.records[0].fields[0], "*", "wildcard adep");
        CheckEqual(r.records[0].fields[1], "SID", "subtype");
        CheckEqual(r.records[1].fields[0], "CYQM,CYYZ", "comma list kept as one field");
        CheckEqual(r.records[1].fields[1], "AHDG+SID", "compound subtype");
    }

    // --- The separator precedence rule, in both directions.
    {
        const ParseResult r = Parse(
            "FREETEXT:R:CLIMB TO FL350 THEN SET ALT=360\n"
            "Some_Key=a:b:c\n");

        Check(!r.records[0].isAssignment, "an = after the first : does not make an assignment");
        CheckEqual(r.records[0].fields[1], "CLIMB TO FL350 THEN SET ALT=360", "the = stays in the text");

        Check(r.records[1].isAssignment, "an = before the first : makes an assignment");
        CheckEqual(r.records[1].value, "a:b:c", "colons stay in the value");
    }

    // --- Variable field counts, which NFREQ has by design.
    {
        const ParseResult r = Parse("NFREQ:CYQM:AAA,BBB:CCC:DDD\n");
        CheckEqual(r.records[0].fields.size(), 4u, "four fields");
        CheckEqual(r.records[0].fields[3], "DDD", "last one");
    }

    // --- An empty trailing field is a field, not an absence.
    {
        const ParseResult r = Parse("LOGIN:AAAA:BBBB:\n");
        CheckEqual(r.records[0].fields.size(), 3u, "trailing empty field counted");
        CheckEqual(r.records[0].fields[2], "", "and is empty");
    }

    // --- Sections carry forward until the next header, and records above the first
    //     header belong to no section.
    {
        const ParseResult r = Parse(
            "Key=1\n"
            "[DCL]\n"
            "DCL:*:*:x\n"
            "DCL:*:SID:y\n"
            "[STATIONS]\n"
            "LOGIN:A:B:C\n");

        CheckEqual(r.records[0].section, "", "above any section");
        CheckEqual(r.records[1].section, "DCL", "first in DCL");
        CheckEqual(r.records[2].section, "DCL", "still in DCL");
        CheckEqual(r.records[3].section, "STATIONS", "moved to STATIONS");
    }

    // --- Line numbers are 1-based and count comments and blanks, so a reported skip
    //     points at the line a person sees in their editor.
    {
        const ParseResult r = Parse(
            "// one\n"
            "\n"
            "LOGIN:A:B:C\n"
            "this line is neither\n");

        CheckEqual(static_cast<size_t>(r.records[0].line), 3u, "record on line 3");
        CheckEqual(r.skippedLines.size(), 1u, "one skipped line");
        CheckEqual(static_cast<size_t>(r.skippedLines[0]), 4u, "and it is line 4");
    }

    // --- CRLF, since these files are edited on Windows.
    {
        const ParseResult r = Parse("[STATIONS]\r\nLOGIN:CZQM:MONCTON CTR:QM\r\n");
        CheckEqual(r.records.size(), 1u, "one record from CRLF input");
        CheckEqual(r.records[0].section, "STATIONS", "section header without a stray bracket");
        CheckEqual(r.records[0].fields[2], "QM", "no carriage return left on the last field");
    }

    // --- Degenerate input does not throw or produce records.
    {
        CheckEqual(Parse("").records.size(), 0u, "empty input");
        CheckEqual(Parse("\n\n\n").records.size(), 0u, "only newlines");
        CheckEqual(Parse("[STATIONS]").records.size(), 0u, "a header alone");
        CheckEqual(Parse("no separator at all").skippedLines.size(), 1u, "a bare word is skipped");
    }

    // --- A file with no trailing newline still yields its last record.
    {
        const ParseResult r = Parse("LOGIN:A:B:C");
        CheckEqual(r.records.size(), 1u, "last line without a newline");
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
