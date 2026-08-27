// Tests for SettingsFile.h.
//
// The property that matters is that nothing a controller has set is lost by writing the
// file out and reading it back, so most of this is round trip. The rest covers the two
// things the JSON it replaces did badly: losing every setting in the file because one
// value was malformed, and throwing out of a EuroScope callback while doing it.
//
//   cl /EHsc /W4 /std:c++17 tests\SettingsFileTests.cpp /Fe:SettingsFileTests.exe
//   SettingsFileTests.exe

#include "../SettingsFile.h"

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

    SituSettings::Settings RoundTrip(const SituSettings::Settings& in)
    {
        return SituSettings::Parse(SituConfig::Parse(SituSettings::Serialize(in)));
    }
}

int main()
{
    using namespace SituSettings;

    // --- Defaults survive a round trip unchanged.
    {
        const Settings defaults;
        const Settings back = RoundTrip(defaults);

        CheckEqual(back.wxLat, defaults.wxLat, "default WxLat");
        CheckEqual(back.wxLong, defaults.wxLong, "default WxLong");
        CheckEqual(back.prefSFI, defaults.prefSFI, "default PrefSFI");
        CheckEqual(back.cpdlcServer, defaults.cpdlcServer, "default CpdlcServer");
        Check(back.historyDots == defaults.historyDots, "default HistoryDots");
        Check(back.bigACID == defaults.bigACID, "default BigACID is true and stays true");
        Check(back.wxAll == defaults.wxAll, "default WxAll");
        Check(back.filterBypassAll == defaults.filterBypassAll, "default FilterBypassAll");
        Check(back.extAltToggle == defaults.extAltToggle, "default ExtAltToggle");
        Check(back.skippedLines.empty(), "our own output has no unreadable lines");
    }

    // --- Every value changed from its default survives. A setting that silently reverts
    //     is the failure a controller notices and cannot explain.
    {
        Settings custom;
        custom.wxLat = "46.1";
        custom.wxLong = "-64.7";
        custom.atisListOffset = { 123, 456 };
        custom.offScreenListOffset = { -7, 89 };
        custom.messageListOffset = { 0, 0 };
        custom.historyDots = 9;
        custom.bigACID = false;
        custom.wxAll = true;
        custom.filterBypassAll = true;
        custom.extAltToggle = true;
        custom.prefSFI = "JIHGFEDCBA";
        custom.cpdlcServer = "http://localhost:8080/acars/system/connect.html";
        for (size_t i = 0; i < kControllerRemarkCount; ++i)
        {
            custom.controllerRemarks[i] = "remark " + std::to_string(i + 1);
        }

        const Settings back = RoundTrip(custom);

        CheckEqual(back.wxLat, "46.1", "WxLat");
        CheckEqual(back.wxLong, "-64.7", "WxLong, negative");
        Check(back.atisListOffset.x == 123 && back.atisListOffset.y == 456, "atis list offset");
        Check(back.offScreenListOffset.x == -7 && back.offScreenListOffset.y == 89, "negative offset component");
        Check(back.messageListOffset.x == 0 && back.messageListOffset.y == 0, "zero offset");
        Check(back.historyDots == 9, "HistoryDots");
        Check(back.bigACID == false, "BigACID false survives, which a truthy default would hide");
        Check(back.wxAll && back.filterBypassAll && back.extAltToggle, "the toggles");
        CheckEqual(back.prefSFI, "JIHGFEDCBA", "PrefSFI");
        CheckEqual(back.cpdlcServer, "http://localhost:8080/acars/system/connect.html",
                   "CpdlcServer pointed at a distributor");

        for (size_t i = 0; i < kControllerRemarkCount; ++i)
        {
            CheckEqual(back.controllerRemarks[i], "remark " + std::to_string(i + 1),
                       "controller remark " + std::to_string(i + 1));
        }
    }

    // --- Remarks are free text and must survive punctuation. An = or a : inside a value
    //     must not be read as structure.
    {
        Settings custom;
        custom.controllerRemarks[0] = "CLIMB VIA SID = OK";
        custom.controllerRemarks[1] = "SEE: NOTAM 12/345";
        custom.controllerRemarks[2] = "  leading and trailing  ";
        custom.prefSFI = "A=B";

        const Settings back = RoundTrip(custom);
        CheckEqual(back.controllerRemarks[0], "CLIMB VIA SID = OK", "an equals inside a value");
        CheckEqual(back.controllerRemarks[1], "SEE: NOTAM 12/345", "a colon inside a value");
        CheckEqual(back.controllerRemarks[2], "  leading and trailing  ", "whitespace inside a value");
        CheckEqual(back.prefSFI, "A=B", "an equals in PrefSFI");
    }

    // --- An empty remark is a value, not an absence.
    {
        Settings custom;
        custom.controllerRemarks[3] = "";
        const Settings back = RoundTrip(custom);
        CheckEqual(back.controllerRemarks[3], "", "an empty remark stays empty");
        Check(back.skippedLines.empty(), "and is not reported as unreadable");
    }

    // --- One bad line costs that line. This is the whole reason for the change: the
    //     JSON this replaces lost every setting in the file when one value was wrong.
    {
        const Settings damaged = Parse(SituConfig::Parse(
            "WxLat=46.1\n"
            "HistoryDots=lots\n"
            "BigACID=maybe\n"
            "AtisListOffset=123\n"
            "OffScreenListOffset=12,\n"
            "PrefSFI=ABC\n"
            "NotASetting=1\n"));

        CheckEqual(damaged.wxLat, "46.1", "the good line before the bad ones survived");
        CheckEqual(damaged.prefSFI, "ABC", "and the good line after them");
        Check(damaged.historyDots == 4, "an unparseable int leaves the default");
        Check(damaged.bigACID == true, "an unparseable bool leaves the default");
        Check(damaged.atisListOffset.x == 500 && damaged.atisListOffset.y == 84,
              "a point with no comma leaves the default");
        Check(damaged.offScreenListOffset.x == 0 && damaged.offScreenListOffset.y == 500,
              "a point with a missing half leaves the default rather than applying the half");
        Check(damaged.skippedLines.size() == 5,
              "five lines reported, not dropped (got " + std::to_string(damaged.skippedLines.size()) + ")");
    }

    // --- Booleans are written as 1 and 0 but several spellings are accepted, because
    //     people edit this by hand.
    {
        const Settings s = Parse(SituConfig::Parse("BigACID=false\nWxAll=true\nExtAltToggle=yes\nFilterBypassAll=no\n"));
        Check(!s.bigACID && s.wxAll && s.extAltToggle && !s.filterBypassAll, "spelled out booleans");
        Check(s.skippedLines.empty(), "and none of them is reported as bad");
    }

    // --- Comments and blank lines are not settings and are not complaints.
    {
        const Settings s = Parse(SituConfig::Parse("// a comment\n\nWxLat=1.0\n   // indented\n"));
        CheckEqual(s.wxLat, "1.0", "the setting was read");
        Check(s.skippedLines.empty(), "comments and blanks are not reported");
    }

    // --- Local settings are a separate file, so a credential cannot be shipped by
    //     accident with a sector package.
    {
        LocalSettings local;
        local.hoppieCode = "abc123XYZ";
        local.hoppieICAO = "CZQM";

        const LocalSettings back = ParseLocal(SituConfig::Parse(SerializeLocal(local)));
        CheckEqual(back.hoppieCode, "abc123XYZ", "logon code round trips");
        CheckEqual(back.hoppieICAO, "CZQM", "station round trips");
        Check(back.skippedLines.empty(), "no unreadable lines");

        const std::string written = SerializeLocal(local);
        Check(written.find("do not commit") != std::string::npos,
              "the file says what it is, since its whole purpose is not being shared");

        // The main settings file must never carry the credential.
        Settings settings;
        const std::string mainFile = Serialize(settings);
        Check(mainFile.find("Hoppie") == std::string::npos
              || mainFile.find("SituLocal.txt") != std::string::npos,
              "settings.txt mentions Hoppie only to point at the local file");
        Check(mainFile.find("HoppieCode=") == std::string::npos,
              "settings.txt has no logon code key at all");
    }

    // --- A blank field in SituLocal.txt is an absence, not an instruction to forget.
    //
    //     Doing this the obvious way lost people's logon codes: the migration recovered
    //     the credential from settings.json, and the read of SituLocal.txt then replaced
    //     the whole struct. Anyone who had copied a blank SituLocal.txt in alongside
    //     their existing settings.json got "error (no logon code)" from Hoppie.
    {
        LocalSettings fromFile;                 // a blank template, as shipped
        LocalSettings fromMigration;
        fromMigration.hoppieCode = "abc123XYZ";
        fromMigration.hoppieICAO = "CZQM";

        FillEmptyFrom(fromFile, fromMigration);
        CheckEqual(fromFile.hoppieCode, "abc123XYZ", "a blank file takes the migrated code");
        CheckEqual(fromFile.hoppieICAO, "CZQM", "and the migrated station");
    }
    {
        LocalSettings fromFile;
        fromFile.hoppieCode = "theRealOne";
        LocalSettings fromMigration;
        fromMigration.hoppieCode = "anOldOne";
        fromMigration.hoppieICAO = "CZQX";

        FillEmptyFrom(fromFile, fromMigration);
        CheckEqual(fromFile.hoppieCode, "theRealOne", "a set field is not overwritten");
        CheckEqual(fromFile.hoppieICAO, "CZQX", "but an unset one beside it is filled");
    }
    {
        LocalSettings fromFile;
        FillEmptyFrom(fromFile, LocalSettings());
        Check(fromFile.hoppieCode.empty(), "nothing anywhere stays nothing");
    }

    // --- Nothing here throws, whatever it is handed. The JSON it replaces threw out of
    //     a EuroScope callback on a malformed file.
    {
        const char* const nasty[] = {
            "", "\n\n\n", "=", "=value", "Key=", "::::", "[section]",
            "WxLat", "AtisListOffset=,", "AtisListOffset=,,", "HistoryDots=999999999999",
            "CtrlRemark0=x", "CtrlRemark8=x", "CtrlRemark=x", "CtrlRemarkA=x"
        };
        bool threw = false;
        for (const char* input : nasty)
        {
            try { Parse(SituConfig::Parse(input)); ParseLocal(SituConfig::Parse(input)); }
            catch (...) { threw = true; std::cout << "   threw on: " << input << "\n"; }
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
