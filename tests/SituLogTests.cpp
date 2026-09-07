// Tests for SituLogCore.h.
//
//   cl /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:SituLogTests.exe
//   SituLogTests.exe

#include "../SituLogCore.h"

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

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << "\n   expected: \"" << expected << "\"\n   got:      \"" << actual << "\"\n";
        }
    }
}

int main()
{
    using namespace SituLog;

    // --- Formatter. Column widths are fixed so grep and eye both find the field.
    {
        CheckEqual(FormatLine("02:15:58.412", "ES>", "CORRELATE", Fields().Add("callsign", "ACA123").Add("squawk", "4521")),
                   "02:15:58.412 ES>  CORRELATE   callsign=ACA123 squawk=4521",
                   "two plain fields");

        CheckEqual(FormatLine("02:15:58.412", "DRAW", "ACA123", Fields()),
                   "02:15:58.412 DRAW ACA123      ",
                   "no fields still pads the subject");

        // Category is padded to 4, subject to 11, each followed by one space.
        CheckEqual(FormatLine("00:00:00.000", "CMD", "log", Fields().Add("args", "on")),
                   "00:00:00.000 CMD  log         args=on",
                   "short category and subject padded");

        // A subject longer than 11 is not truncated.
        CheckEqual(FormatLine("00:00:00.000", "EVT", "OnGetTagItemXYZ", Fields()),
                   "00:00:00.000 EVT  OnGetTagItemXYZ ",
                   "long subject kept whole, one trailing space");

        // Quoting: space, quote and equals each force quotes; quotes inside are doubled.
        CheckEqual(QuoteValue("YHZ MIILS"), "\"YHZ MIILS\"", "space quoted");
        CheckEqual(QuoteValue("a=b"), "\"a=b\"", "equals quoted");
        CheckEqual(QuoteValue("say \"hi\""), "\"say \"\"hi\"\"\"", "quote doubled and quoted");
        CheckEqual(QuoteValue("plain"), "plain", "plain not quoted");
        CheckEqual(QuoteValue(""), "", "empty stays empty");

        CheckEqual(FormatLine("00:00:00.000", "EVT", "FP-DATA", Fields().Add("rmk", "").Add("route", "A B")),
                   "00:00:00.000 EVT  FP-DATA     rmk= route=\"A B\"",
                   "empty value visible as key=, spaced value quoted");

        // Typed adds.
        CheckEqual(FormatLine("00:00:00.000", "DRAW", "X", Fields().Add("corr", true).Add("adsb", false).Add("flags", 6).Add("ms", 12.5)),
                   "00:00:00.000 DRAW X           corr=1 adsb=0 flags=6 ms=12.5",
                   "bool as 0/1, int, double");

        // Route truncation helper: 60 characters then "...".
        const std::string longRoute(70, 'R');
        CheckEqual(Truncate(longRoute, 60), std::string(60, 'R') + "...", "truncated at 60 with ellipsis");
        CheckEqual(Truncate("short", 60), "short", "short untouched");
    }

    // --- Command parser. Anything not beginning ".situ log" is not ours and must not be
    //     claimed, or the plugin would swallow other plugins' commands and chat.
    {
        Check(ParseLogCommand(".situ log on").action == LogAction::On, "on");
        Check(ParseLogCommand(".situ log off").action == LogAction::Off, "off");
        Check(ParseLogCommand(".situ log all").action == LogAction::FollowAll, "all");
        Check(ParseLogCommand(".situ log none").action == LogAction::Unfollow, "none");
        Check(ParseLogCommand(".situ log status").action == LogAction::Status, "status");
        Check(ParseLogCommand(".situ log").action == LogAction::Help, "bare gives help");
        Check(ParseLogCommand(".situ log   ").action == LogAction::Help, "bare with spaces gives help");
        Check(ParseLogCommand(".situ log on extra").action == LogAction::Help, "extra words give help");

        // Keywords are case-insensitive; so is the prefix.
        Check(ParseLogCommand(".SITU LOG ON").action == LogAction::On, "upper-case keyword");
        Check(ParseLogCommand("  .situ log off").action == LogAction::Off, "leading spaces");

        // A callsign is anything that is not a keyword, upper-cased.
        {
            const LogCommand c = ParseLogCommand(".situ log aca123");
            Check(c.action == LogAction::Follow, "callsign follows");
            CheckEqual(c.arg, "ACA123", "callsign upper-cased");
        }
        {
            const LogCommand c = ParseLogCommand(".situ log CGNQC");
            Check(c.action == LogAction::Follow && c.arg == "CGNQC", "registration follows");
        }

        // Not ours.
        Check(ParseLogCommand(".situ").action == LogAction::NotOurs, ".situ alone is not ours");
        Check(ParseLogCommand(".situlog on").action == LogAction::NotOurs, "no space is not ours");
        Check(ParseLogCommand(".am ACA123").action == LogAction::NotOurs, "other dot command");
        Check(ParseLogCommand("hello").action == LogAction::NotOurs, "chat");
        Check(ParseLogCommand("").action == LogAction::NotOurs, "empty");
    }

    // --- Rotation. Given the names in the folder, which to delete so that after the new
    //     file is created at most five exist. keep is the number of OLD files to keep (4).
    {
        Check(IsLogFileName("SituDebug-20260907-021558.log"), "canonical name matches");
        Check(!IsLogFileName("settings.txt"), "settings not a log");
        Check(!IsLogFileName("SituDebug-20260907-021558.log.bak"), "wrong suffix");
        Check(!IsLogFileName("Debug-20260907.log"), "wrong prefix");
        CheckEqual(LogFileName("20260907-021558"), "SituDebug-20260907-021558.log", "name built from stamp");

        std::vector<std::string> none;
        Check(RotationVictims(none, 4).empty(), "empty folder, nothing to delete");

        std::vector<std::string> four = {
            "SituDebug-20260901-000000.log", "SituDebug-20260902-000000.log",
            "SituDebug-20260903-000000.log", "SituDebug-20260904-000000.log" };
        Check(RotationVictims(four, 4).empty(), "four old files, all kept");

        std::vector<std::string> five = four;
        five.push_back("SituDebug-20260905-000000.log");
        {
            const std::vector<std::string> v = RotationVictims(five, 4);
            Check(v.size() == 1 && v[0] == "SituDebug-20260901-000000.log", "five old files, oldest deleted");
        }

        // Order in the input does not matter; names sort by their timestamp. Nine files with
        // four kept means the five oldest go, which here ends at the earlier of the two
        // 20260906 files -- the later one survives, so same-day ordering is exercised.
        std::vector<std::string> shuffled = {
            "SituDebug-20260909-000000.log", "SituDebug-20260901-000000.log",
            "SituDebug-20260903-000000.log", "SituDebug-20260902-000000.log",
            "SituDebug-20260904-000000.log", "SituDebug-20260906-120000.log",
            "SituDebug-20260906-110000.log", "SituDebug-20260907-000000.log",
            "SituDebug-20260908-000000.log" };
        {
            const std::vector<std::string> v = RotationVictims(shuffled, 4);
            Check(v.size() == 5, "nine old files, five deleted");
            Check(v.size() == 5 && v[0] == "SituDebug-20260901-000000.log", "oldest first");
            Check(v.size() == 5 && v[4] == "SituDebug-20260906-110000.log", "same day, earlier time deleted");
        }

        // Other files in the folder are never victims.
        std::vector<std::string> mixed = five;
        mixed.push_back("settings.txt");
        mixed.push_back("SituLocal.txt");
        mixed.push_back("0_0.png");
        {
            const std::vector<std::string> v = RotationVictims(mixed, 4);
            Check(v.size() == 1 && v[0] == "SituDebug-20260901-000000.log", "non-log files untouched");
        }
    }

    // --- Draw snapshot. Equality is field-by-field; the fields list is the full state.
    {
        DrawSnapshot a;
        a.flags = 6; a.corr = true; a.sqk = "4521"; a.trk = "QM"; a.tag = 1;
        a.pps = "HEXAGON"; a.colour = "YELLOW"; a.tagfn = "ALPHA";

        DrawSnapshot b = a;
        Check(a == b, "copy is equal");
        Check(!(a != b), "copy is not unequal");

        b.sqk = "1200";
        Check(a != b, "squawk change is a change");

        b = a; b.pps = "ASTERISK";
        Check(a != b, "shape change is a change");

        b = a; b.vf = true;
        Check(a != b, "vf change is a change");

        b = a; b.flags = 7;
        Check(a != b, "flags change is a change");

        CheckEqual(FormatLine("00:00:00.000", "DRAW", "ACA123", SnapshotFields(a)),
                   "00:00:00.000 DRAW ACA123      flags=6 corr=1 adsb=0 rvsm=0 vfr=0 sqk=4521 trk=QM tag=1 pps=HEXAGON colour=YELLOW vf=0 tagfn=ALPHA",
                   "snapshot fields in the documented order");
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) { std::cout << g_failures << " FAILURES\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
