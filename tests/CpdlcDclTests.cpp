// Tests for CpdlcDcl.h.
//
// The point of these is that the shipped rows produce exactly what the compiled branches
// produced. The old string building is reproduced verbatim below, run against the same
// inputs, and required to match - except where the old code was wrong, which is asserted
// separately so the difference is deliberate and visible.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcDclTests.cpp /Fe:CpdlcDclTests.exe
//   CpdlcDclTests.exe

#include "../CpdlcDcl.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
            std::cout << "FAIL: " << what << "\n  expected: " << expected << "\n  got:      " << actual << "\n";
        }
    }

    std::string ReadFile(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    // One flight's worth of the values the templates name.
    struct Flight
    {
        std::string adep = "CYTZ";
        std::string ades = "CYVR";
        std::string callsign = "ACA7281";
        std::string number = "360";
        std::string letter = "M";
        std::string drwy = "33R";
        std::string sid = "AVSEP6";
        std::string assr = "2264";
        std::string atis = "H";
        std::string uname = "CYTZ_TWR";
        std::string freq = "118.200";
        std::string time = "1003";
        std::string date = "260622";
        std::string actype = "H/B77W/W";
        std::string cfl = "FL360";
        std::string rte = "AVSEP6 MUSIT SSM YQT GERTY";
    };

    std::vector<SituCpdlcDcl::Field> FieldsFor(const Flight& f)
    {
        return {
            { "adep", f.adep }, { "ades", f.ades }, { "callsign", f.callsign },
            { "number", f.number }, { "letter", f.letter }, { "drwy", f.drwy },
            { "sid", f.sid }, { "assr", f.assr }, { "atis", f.atis },
            { "uname", f.uname }, { "freq", f.freq }, { "time", f.time },
            { "date", f.date }, { "actype", f.actype }, { "cfl", f.cfl }, { "rte", f.rte },
        };
    }

    // The compiled CYTZ branch, verbatim.
    std::string OldToronto(const Flight& f)
    {
        std::string m;
        m += f.adep;
        m += " PDC ";
        m += f.number;
        m += " ";
        m += f.callsign;
        m += " CLRD TO ";
        m += f.ades;
        m += " OFF ";
        m += f.drwy;
        m += " VIA ";
        m += f.sid;
        m += " SQUAWK ";
        m += f.assr;
        m += " ATIS ";
        m += f.atis;
        m += " CONTACT ";
        m += f.uname;
        m += " ON FREQ ";
        m += f.freq;
        return m;
    }

    // The compiled CYUL branch, verbatim - including the assignment on the second line
    // where every other line appends, which is the defect.
    std::string OldMontreal(const Flight& f)
    {
        std::string m;
        m += f.adep;
        m = " PDC ";   // '=' not '+=' : the origin just appended is discarded here
        m += f.number;
        m += " ";
        m += f.callsign;
        m += " CLRD TO ";
        m += f.ades;
        m += " OFF ";
        m += f.drwy;
        m += " VIA ";
        m += f.sid;
        m += " SQUAWK ";
        m += f.assr;
        m += " NEXT FREQ ";
        m += f.freq;
        m += " ATIS ";
        m += f.atis;
        return m;
    }
}

int main(int argc, char** argv)
{
    using namespace SituCpdlcDcl;

    const char* path = (argc > 1) ? argv[1] : "SituCPDLC.txt";
    const std::string text = ReadFile(path);
    Check(!text.empty(), std::string("the shipped file was found: ") + path);
    if (text.empty()) { std::cout << "cannot continue without the file\n"; return 1; }

    const Table table = Parse(SituConfig::Parse(text));
    Check(table.rules.size() == 5, "five DCL rows read (got " + std::to_string(table.rules.size()) + ")");
    Check(table.skippedLines.empty(), "no DCL row in the shipped file is unreadable");

    // --- Toronto reproduces the compiled branch exactly.
    {
        Flight f;
        f.adep = "CYTZ";
        const Rule* rule = Select(table, "CYTZ", "");
        Check(rule != nullptr, "a rule matches CYTZ");
        if (rule != nullptr)
        {
            CheckEqual(Format(rule->text, FieldsFor(f)), OldToronto(f), "CYTZ matches the compiled branch");
            CheckEqual(rule->messageType, "cpdlc", "CYTZ is a cpdlc message");
            CheckEqual(rule->responseRequired, "WU", "CYTZ wants wilco or unable");
        }
    }

    // --- Montreal deliberately does NOT reproduce the compiled branch, because the
    //     compiled branch dropped the origin.
    {
        Flight f;
        f.adep = "CYUL";
        const Rule* rule = Select(table, "CYUL", "");
        Check(rule != nullptr, "a rule matches CYUL");
        if (rule != nullptr)
        {
            const std::string produced = Format(rule->text, FieldsFor(f));

            Check(produced != OldMontreal(f), "CYUL differs from the compiled branch, which is the fix");
            Check(produced.compare(0, 4, "CYUL") == 0, "the origin is present now");
            Check(OldMontreal(f).compare(0, 4, " PDC") == 0, "and was absent before");

            // Identical once the missing origin is accounted for.
            CheckEqual(produced, f.adep + OldMontreal(f), "otherwise byte for byte the same");
        }
    }

    // --- Everywhere else falls through to the long telex form.
    {
        Flight f;
        f.adep = "CYQM";
        const Rule* rule = Select(table, "CYQM", "");
        Check(rule != nullptr, "a rule matches an airport with no row of its own");
        if (rule != nullptr)
        {
            CheckEqual(rule->messageType, "telex", "and it is a telex");

            const std::string produced = Format(rule->text, FieldsFor(f));
            Check(produced.find("PRE-DEPARTURE CLEARANCE") != std::string::npos, "the long form");
            Check(produced.find("CYQM") != std::string::npos, "naming the departure airport");
            Check(produced.find("360M") != std::string::npos, "the identifier is number then letter");
            Check(produced.find("AVSEP6 MUSIT SSM YQT GERTY END") != std::string::npos, "route then END");
        }
    }

    // --- The subtypes, which are selected by name rather than by airport.
    {
        const Rule* fsm = Select(table, "CYQM", "FSM");
        Check(fsm != nullptr, "FSM has a rule");
        if (fsm != nullptr)
        {
            Flight f;
            f.adep = "CYQM";
            CheckEqual(Format(fsm->text, FieldsFor(f)),
                       "FSM 1003 260622 CYQMRCD RECEIVED @REQUEST BEING PROCESSED @STANDBY",
                       "FSM matches the compiled string, missing space and all");
        }

        const Rule* reject = Select(table, "CYQM", "FSMREJECT");
        Check(reject != nullptr, "the rejection has a rule");
        if (reject != nullptr)
        {
            CheckEqual(Format(reject->text, FieldsFor(Flight())),
                       "RCD REJECTED @FLIGHT PLAN NOT HELD @REVERT TO VOICE PROCEDURES",
                       "and matches too");
        }
    }

    // --- First match wins, and a specific row beats a wildcard placed after it.
    {
        const Table ordered = Parse(SituConfig::Parse(
            "[DCL]\nDCL:CYTZ:*:cpdlc:WU:SPECIFIC\nDCL:*:*:telex:NE:GENERAL\n"));
        CheckEqual(Select(ordered, "CYTZ", "")->text, "SPECIFIC", "the specific row wins");
        CheckEqual(Select(ordered, "CYQM", "")->text, "GENERAL", "the wildcard covers the rest");

        const Table reversed = Parse(SituConfig::Parse(
            "[DCL]\nDCL:*:*:telex:NE:GENERAL\nDCL:CYTZ:*:cpdlc:WU:SPECIFIC\n"));
        CheckEqual(Select(reversed, "CYTZ", "")->text, "GENERAL",
                   "a wildcard placed first shadows what follows, as documented");
    }

    // --- Comma separated departure lists.
    {
        const Table list = Parse(SituConfig::Parse(
            "[DCL]\nDCL:CYTZ,CYUL, CYYZ:*:cpdlc:WU:LISTED\nDCL:*:*:telex:NE:OTHER\n"));
        CheckEqual(Select(list, "CYTZ", "")->text, "LISTED", "first in the list");
        CheckEqual(Select(list, "CYYZ", "")->text, "LISTED", "last in the list, with a leading space");
        CheckEqual(Select(list, "CYOW", "")->text, "OTHER", "not in the list");
    }

    // --- A colon inside a clearance survives, which is why the text is the remainder
    //     rather than a field.
    {
        const Table colons = Parse(SituConfig::Parse(
            "[DCL]\nDCL:*:*:telex:NE:TIMESTAMP 10:03 CLRD TO <ades>\n"));
        Check(colons.rules.size() == 1, "the row parsed");
        CheckEqual(Format(colons.rules[0].text, FieldsFor(Flight())),
                   "TIMESTAMP 10:03 CLRD TO CYVR", "the colon is part of the text");
    }

    // --- A placeholder nobody supplied is left visible rather than silently blanked.
    {
        CheckEqual(Format("CLRD TO <ades> VIA <nosuchfield>", FieldsFor(Flight())),
                   "CLRD TO CYVR VIA <nosuchfield>", "an unknown placeholder stays put");
        CheckEqual(Format("<ades", FieldsFor(Flight())), "<ades", "an unterminated placeholder is text");
        CheckEqual(Format("", FieldsFor(Flight())), "", "empty template");
        CheckEqual(Format("no placeholders", {}), "no placeholders", "no fields at all");
    }

    // --- Short or empty rows cost their line.
    {
        const Table damaged = Parse(SituConfig::Parse(
            "[DCL]\nDCL:CYTZ\nDCL:CYTZ:*:cpdlc\nDCL:CYTZ:*:cpdlc:WU:\nDCL:*:*:telex:NE:GOOD\n"));
        Check(damaged.rules.size() == 1, "one sound row survived");
        Check(damaged.skippedLines.size() == 3,
              "three bad rows reported (got " + std::to_string(damaged.skippedLines.size()) + ")");
    }

    // --- No rule at all means no clearance, not an empty one.
    {
        const Table empty = Parse(SituConfig::Parse("[DCL]\nDCL:CYTZ:*:cpdlc:WU:X\n"));
        Check(Select(empty, "CYQM", "") == nullptr, "an unmatched airport selects nothing");
    }

    // --- Nothing throws.
    {
        bool threw = false;
        const char* const nasty[] = { "", "DCL:", "DCL:::::", "[DCL]", "DCL:*:*:*:*:<" };
        for (const char* raw : nasty)
        {
            try { const Table t = Parse(SituConfig::Parse(raw)); Select(t, "", ""); Format(raw, {}); }
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
