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

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) { std::cout << g_failures << " FAILURES\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
