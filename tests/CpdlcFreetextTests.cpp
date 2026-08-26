// Tests for CpdlcFreetext.h, run against the shipped SituCPDLC.txt.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcFreetextTests.cpp /Fe:CpdlcFreetextTests.exe
//   CpdlcFreetextTests.exe

#include "../CpdlcFreetext.h"

#include <fstream>
#include <iostream>
#include <sstream>
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

    std::string ReadFile(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    std::string ReplyTypeOf(const SituCpdlcFreetext::Table& table, const std::string& text)
    {
        const SituCpdlcFreetext::Entry* entry = SituCpdlcFreetext::Find(table, text);
        return entry != nullptr ? entry->replyType : std::string("(not found)");
    }
}

int main(int argc, char** argv)
{
    using namespace SituCpdlcFreetext;

    const char* path = (argc > 1) ? argv[1] : "SituCPDLC.txt";
    const std::string text = ReadFile(path);
    Check(!text.empty(), std::string("the shipped file was found: ") + path);
    if (text.empty()) { std::cout << "cannot continue without the file\n"; return 1; }

    const Table table = Parse(SituConfig::Parse(text));
    Check(table.entries.size() == 15, "15 freetext entries read (got " + std::to_string(table.entries.size()) + ")");
    Check(table.skippedLines.empty(), "no freetext row in the shipped file is unreadable");

    // --- The reply types the window's buttons need, from the file rather than from a
    //     table in the source. These are what a controller edits when the wording of a
    //     standing message changes.
    CheckEqual(ReplyTypeOf(table, "UNABLE"), "R", "UNABLE");
    CheckEqual(ReplyTypeOf(table, "STANDBY"), "R", "STANDBY");
    CheckEqual(ReplyTypeOf(table, "ROGER"), "NE", "ROGER expects no reply");
    CheckEqual(ReplyTypeOf(table, "AFFIRMATIVE"), "NE", "AFFIRMATIVE");
    CheckEqual(ReplyTypeOf(table, "NEGATIVE"), "NE", "NEGATIVE");
    CheckEqual(ReplyTypeOf(table, "CONTACT ATC ON VOICE"), "WU", "a wilco or unable message");

    // --- Buttons are labelled in title case; the wire text is upper case.
    CheckEqual(ReplyTypeOf(table, "Unable"), "R", "title case finds it");
    CheckEqual(ReplyTypeOf(table, "roger"), "NE", "lower case too");

    // --- UNICOM is normalised to WU, and the behaviour behind it is recorded rather
    //     than inherited. TopSky drops the track on wilco; we do not.
    {
        const Entry* unicom = Find(table, "SERVICES TERMINATED - MNT UNICOM 122.8");
        Check(unicom != nullptr, "the UNICOM entry is present");
        if (unicom != nullptr)
        {
            CheckEqual(unicom->replyType, "WU", "UNICOM is sent as wilco or unable");
            Check(unicom->terminatesService, "and the terminating behaviour is recorded");
        }

        int terminators = 0;
        for (const Entry& entry : table.entries) { if (entry.terminatesService) { ++terminators; } }
        Check(terminators == 1, "exactly one entry carries it");
    }

    // --- A message the file does not have yields nothing rather than a default. A
    //     button with nothing behind it should say so, not send a reply type nobody
    //     chose.
    Check(Find(table, "NOT IN THE FILE") == nullptr, "an unknown message is not found");
    Check(Find(table, "") == nullptr, "an empty lookup finds nothing");

    // --- Text with punctuation in it, which the shipped file has.
    Check(Find(table, "SUBMIT REQ FOR NAT CROSSING VIA NATTRAK.VATSIM.NET") != nullptr,
          "a dotted hostname is part of the text");
    Check(Find(table, "OUT OF COCKPIT REQ APV - CTC ON VOICE ONCE RETURNED.") != nullptr,
          "a trailing period is part of the text");

    // --- A colon inside a message does not split it.
    {
        const Table colons = Parse(SituConfig::Parse("[FREETEXT]\nFREETEXT:R:EXPECT AT 12:30\n"));
        Check(colons.entries.size() == 1, "the row parsed");
        if (!colons.entries.empty()) { CheckEqual(colons.entries[0].text, "EXPECT AT 12:30", "colon kept"); }
    }

    // --- Bad rows cost their line.
    {
        const Table damaged = Parse(SituConfig::Parse(
            "[FREETEXT]\nFREETEXT:R:GOOD\nFREETEXT:XX:BAD TYPE\nFREETEXT:R:\nFREETEXT:R\n"));
        Check(damaged.entries.size() == 1, "one sound entry survived");
        Check(damaged.skippedLines.size() == 3,
              "three bad rows reported (got " + std::to_string(damaged.skippedLines.size()) + ")");
    }

    // --- Nothing throws.
    {
        bool threw = false;
        const char* const nasty[] = { "", "FREETEXT:", "FREETEXT::", "[FREETEXT]", ":::" };
        for (const char* raw : nasty)
        {
            try { const Table t = Parse(SituConfig::Parse(raw)); Find(t, raw); }
            catch (...) { threw = true; std::cout << "   threw on: " << raw << "\n"; }
        }
        Check(!threw, "no input throws");
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
