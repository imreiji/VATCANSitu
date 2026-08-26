// Tests for CpdlcStations.h.
//
// Most of these run against the shipped SituCPDLC.txt rather than invented input,
// because the thing worth checking is that the file we actually ship resolves the
// positions a CZQM controller actually hands to.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcStationsTests.cpp /Fe:CpdlcStationsTests.exe
//   CpdlcStationsTests.exe

#include "../CpdlcStations.h"

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

    // Resolve and return the login, or a marker so a miss is visible in the message.
    std::string LoginFor(const SituCpdlcStations::Table& table,
                         const std::string& positionId,
                         const std::string& callsign)
    {
        const SituCpdlcStations::Resolution r = SituCpdlcStations::Resolve(table, positionId, callsign);
        return r.found ? r.login : std::string("(unresolved)");
    }
}

int main(int argc, char** argv)
{
    using namespace SituCpdlcStations;

    const char* path = (argc > 1) ? argv[1] : "SituCPDLC.txt";
    const std::string text = ReadFile(path);
    Check(!text.empty(), std::string("the shipped file was found: ") + path);
    if (text.empty()) { std::cout << "cannot continue without the file\n"; return 1; }

    const Table table = Parse(SituConfig::Parse(text));

    Check(table.stations.size() == 141, "141 station rows read (got " + std::to_string(table.stations.size()) + ")");
    Check(table.facilities.size() == 16, "16 facility rows read (got " + std::to_string(table.facilities.size()) + ")");
    Check(table.skippedLines.empty(), "nothing in the shipped file is unreadable");

    // --- The exact match path, on positions from the CZQM package.
    CheckEqual(LoginFor(table, "QM", "CZQM_CTR"), "CZQM", "Moncton combined");
    CheckEqual(LoginFor(table, "QM2", "CZQM_2_CTR"), "CZQM", "a Moncton split, which the TopSky table alone missed");
    CheckEqual(LoginFor(table, "ZV", "CZQM_ZV_CTR"), "CZQM", "an irregular Moncton identifier");
    CheckEqual(LoginFor(table, "AY", "MTL_AY_CTR"), "CZUL", "Montreal signs on as MTL and logs on as CZUL");
    CheckEqual(LoginFor(table, "FS1", "CZEG_FS1_CTR"), "CZEG", "Edmonton");
    CheckEqual(LoginFor(table, "GDA", "CZQO_A_DEL"), "ZQOH", "Gander Oceanic, from the TopSky rows");
    CheckEqual(LoginFor(table, "NAT", "NAT_FSS"), "NATX", "Shanwick and Gander, three character identifier");

    // --- The domestic and oceanic Gander distinction, which is the one duplicate in the
    //     data and the reason the tiebreak exists.
    {
        Check(table.duplicateControllerIds.size() == 1,
              "exactly one duplicated controller id (got " + std::to_string(table.duplicateControllerIds.size()) + ")");
        if (!table.duplicateControllerIds.empty())
        {
            CheckEqual(table.duplicateControllerIds[0], "QX", "and it is QX");
        }

        // CDQX is Gander CTR domestic, CZQX is Gander Radio oceanic; both claim QX.
        CheckEqual(LoginFor(table, "QX", "CZQX_CTR"), "CZQX",
                   "a CZQX callsign takes the station whose login matches its prefix");
        CheckEqual(LoginFor(table, "QX", "CDQX_CTR"), "CDQX",
                   "and a CDQX callsign takes the other one");

        // With no prefix to go on it still answers rather than refusing.
        const Resolution blind = Resolve(table, "QX", "");
        Check(blind.found, "an unknown callsign still resolves QX to something");
    }

    // --- The facility fallback: an identifier this table has never heard of, which is
    //     what a different sector package produces.
    {
        const Resolution r = Resolve(table, "ZZ9", "CZQM_ZZ9_CTR");
        Check(r.found && r.viaFacility, "an unknown Moncton identifier falls back on the prefix");
        CheckEqual(r.login, "CZQM", "and still reaches Moncton");

        const Resolution boston = Resolve(table, "01", "BOS_01_CTR");
        Check(boston.found && boston.viaFacility, "Boston resolves by facility");
        CheckEqual(boston.login, "KUSA", "to the shared US domestic station");
    }

    // --- Neither path has anything: report it rather than inventing a station. Sending
    //     an aircraft to a station nobody is listening on fails at the far end, where
    //     the controller cannot see it.
    {
        const Resolution r = Resolve(table, "ZZ9", "LFPG_N_CTR");
        Check(!r.found, "an unknown facility is unresolved");
        CheckEqual(r.login, "", "and yields no login at all");

        Check(!Resolve(table, "", "").found, "nothing in, nothing out");
    }

    // --- An exact station match beats the facility fallback.
    {
        const Resolution r = Resolve(table, "QM", "CZQM_CTR");
        Check(r.found && !r.viaFacility, "the station table answers before the fallback does");
    }

    // --- The handover template. The default is what VATCANSitu has always sent.
    {
        const Resolution r = Resolve(table, "QX", "CZQX_CTR");
        CheckEqual(FormatHandover(table, r), "NEXT DATA AUTHORITY @CZQX@", "the default template");

        Table handover = table;
        handover.handoverTemplate = "HANDOVER @<station>@";
        CheckEqual(FormatHandover(handover, r), "HANDOVER @CZQX@", "the other candidate verb, by configuration");

        Table spelled = table;
        spelled.handoverTemplate = "CONTACT <radio> LOGON @<station>@";
        CheckEqual(FormatHandover(spelled, r), "CONTACT GANDER RADIO LOGON @CZQX@", "the radio callsign too");

        Table twice = table;
        twice.handoverTemplate = "<station> <station>";
        CheckEqual(FormatHandover(twice, r), "CZQX CZQX", "a token used more than once");

        Table none = table;
        none.handoverTemplate = "NO SUBSTITUTION HERE";
        CheckEqual(FormatHandover(none, r), "NO SUBSTITUTION HERE", "a template with no tokens is left alone");
    }

    // --- The template is read from the file when it is there.
    {
        const Table configured = Parse(SituConfig::Parse(
            "CPDLC_HandoverTemplate=HANDOVER @<station>@\n[STATIONS]\nLOGIN:CZQM:MONCTON CTR:QM\n"));
        const Resolution r = Resolve(configured, "QM", "CZQM_CTR");
        CheckEqual(FormatHandover(configured, r), "HANDOVER @CZQM@", "the template came from the file");
    }

    // --- Malformed rows cost their line, not the table.
    {
        const Table damaged = Parse(SituConfig::Parse(
            "[STATIONS]\nLOGIN:CZQM:MONCTON CTR:QM\nLOGIN:CZQX\nLOGIN::NO LOGIN:QX\nFACILITY:MTL\n"));
        Check(damaged.stations.size() == 1, "one sound station survived");
        Check(damaged.facilities.empty(), "and the short facility row was refused");
        Check(damaged.skippedLines.size() == 3,
              "three bad rows reported (got " + std::to_string(damaged.skippedLines.size()) + ")");
    }

    // --- Callsign prefixes.
    CheckEqual(CallsignPrefix("CZQM_2_CTR"), "CZQM", "prefix of a split");
    CheckEqual(CallsignPrefix("MTL_AY_CTR"), "MTL", "prefix of a Montreal position");
    CheckEqual(CallsignPrefix("NAT_FSS"), "NAT", "prefix of an FSS");
    CheckEqual(CallsignPrefix("CZQM"), "CZQM", "a callsign with no underscore is its own prefix");
    CheckEqual(CallsignPrefix(""), "", "empty");

    // --- Nothing throws.
    {
        bool threw = false;
        const char* const nasty[] = { "", "_", "__", "LOGIN:", "FACILITY:", ":::", "[STATIONS]" };
        for (const char* raw : nasty)
        {
            try
            {
                const Table t = Parse(SituConfig::Parse(raw));
                Resolve(t, raw, raw);
                FormatHandover(t, Resolution());
            }
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
