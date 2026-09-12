// Tests for AltitudeEntry.h.
//
//   cl /EHsc /W4 /std:c++17 tests\AltitudeEntryTests.cpp /Fe:AltitudeEntryTests.exe
//   AltitudeEntryTests.exe

#include "../AltitudeEntry.h"

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

    void CheckParse(const std::string& in, bool ok, int value, const std::string& what)
    {
        ++g_checks;
        const SituAltitude::Entry e = SituAltitude::Parse(in);
        if (e.ok != ok || (ok && e.clearedAltitude != value))
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - \"" << in << "\" -> ok=" << e.ok
                      << " value=" << e.clearedAltitude << ", expected ok=" << ok << " value=" << value << "\n";
        }
    }

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected \"" << expected << "\", got \"" << actual << "\"\n";
        }
    }
}

int main()
{
    using namespace SituAltitude;

    // --- Parsing what was typed or picked. EuroScope's cleared altitude is an int in
    //     feet with three reserved values: 0 none, 1 cleared ILS, 2 cleared visual.
    CheckParse("360", true, 36000, "three digits are hundreds of feet");
    CheckParse("010", true, 1000, "leading zero kept");
    CheckParse("600", true, 60000, "top of the list");
    CheckParse("CA", true, 1, "CA is cleared approach, EuroScope value 1");
    CheckParse("VA", true, 2, "VA is visual approach, EuroScope value 2");
    CheckParse("CLR", true, 0, "CLR clears the altitude");
    CheckParse("", true, 0, "empty clears too");
    CheckParse(" 360 ", true, 36000, "surrounding spaces ignored");
    CheckParse("ca", true, 1, "case-insensitive");
    CheckParse("clr", true, 0, "case-insensitive CLR");

    CheckParse("36", false, 0, "two digits rejected - ambiguous");
    CheckParse("3600", false, 0, "four digits rejected - not the tag's unit");
    CheckParse("36000", false, 0, "feet rejected");
    CheckParse("ABC", false, 0, "letters rejected");
    CheckParse("FL360", false, 0, "prefix rejected");
    CheckParse("000", true, 0, "000 is a clear, same as CLR");
    CheckParse("999", true, 99900, "no range check here; the list bounds what is offered");

    // --- The reverse: what the field shows for a stored value.
    CheckEqual(EntryFor(36000), "360", "feet to three digits");
    CheckEqual(EntryFor(1000), "010", "zero padded");
    CheckEqual(EntryFor(1), "CA", "1 shows as CA");
    CheckEqual(EntryFor(2), "VA", "2 shows as VA");
    CheckEqual(EntryFor(0), "", "none shows empty, not CLR");
    CheckEqual(EntryFor(36050), "360", "odd feet truncate");

    // --- The list: 600 down to 010 in tens, then CA, VA, CLR.
    {
        const std::vector<std::string> rows = ListRows();
        Check(rows.size() == 63, "60 levels plus three words");
        Check(rows.size() == 63 && rows[0] == "600", "starts at 600");
        Check(rows.size() == 63 && rows[59] == "010", "last level is 010");
        Check(rows.size() == 63 && rows[60] == "CA" && rows[61] == "VA" && rows[62] == "CLR", "words at the bottom in order");
        Check(rows.size() == 63 && rows[24] == "360", "360 is row 24");
    }

    // --- Which row to highlight when the window opens. A real cleared altitude selects
    //     its own row; none or an approach clearance selects the aircraft's current level
    //     so the list opens where the aircraft is, not at the top.
    Check(RowFor(36000, 12000) == 24, "cleared 360 selects row 24 regardless of current level");
    Check(RowFor(0, 12000) == 48, "no clearance: current 120 selects row 48");
    Check(RowFor(1, 33200) == 27, "CA: current 332 rounds to 330, row 27");
    Check(RowFor(2, 33600) == 26, "VA: current 336 rounds to 340, row 26");
    Check(RowFor(0, 200) == 59, "current below 010 clamps to 010");
    Check(RowFor(0, 70000) == 0, "current above 600 clamps to 600");
    Check(RowFor(1000, 0) == 59, "cleared 010 is row 59");
    Check(RowFor(500, 0) == 59, "cleared 005 clamps to 010");

    // --- Which row is at the top of a seven-row view so the selected one is centred,
    //     clamped at both ends of the list.
    Check(FirstVisibleRow(24, 7, 63) == 21, "360 centred: 390 at the top");
    Check(FirstVisibleRow(0, 7, 63) == 0, "600 selected: top of list");
    Check(FirstVisibleRow(2, 7, 63) == 0, "580 selected: still top");
    Check(FirstVisibleRow(62, 7, 63) == 56, "CLR selected: bottom of list");
    Check(FirstVisibleRow(59, 7, 63) == 56, "010 selected: bottom, words visible below it");
    Check(FirstVisibleRow(3, 7, 5) == 0, "list shorter than the view");

    // --- Climb or descend, for the CPDLC uplink. Compared against the aircraft's
    //     current altitude; an approach or clear value is not a level and gets neither.
    Check(UplinkFor(36000, 33000) == Uplink::Climb, "above current: climb");
    Check(UplinkFor(30000, 33000) == Uplink::Descend, "below current: descend");
    Check(UplinkFor(33000, 33000) == Uplink::None, "same level: nothing to send");
    Check(UplinkFor(1, 33000) == Uplink::None, "CA is not a level");
    Check(UplinkFor(2, 33000) == Uplink::None, "VA is not a level");
    Check(UplinkFor(0, 33000) == Uplink::None, "clear is not a level");

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) { std::cout << g_failures << " FAILURES\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
