// Tests for CpdlcErrors.h.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcErrorTests.cpp /Fe:CpdlcErrorTests.exe
//   CpdlcErrorTests.exe
//
// The strings below are measured, not invented. "error (no logon code)" was reproduced
// against hoppie.nl during this project; "error {you successfully reached the ACARS
// system...}" and "error {logon code not registered with this service}" were measured
// against the CZQM relay; the "callsign already in use" wording is quoted from Hoppie's
// own tech documentation.

#include "../CpdlcErrors.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    const char* Name(SituCpdlcErrors::Severity s)
    {
        switch (s)
        {
        case SituCpdlcErrors::Ok:          return "Ok";
        case SituCpdlcErrors::Transient:   return "Transient";
        case SituCpdlcErrors::Operational: return "Operational";
        case SituCpdlcErrors::Fatal:       return "Fatal";
        }
        return "?";
    }

    void CheckClass(const std::string& reply, SituCpdlcErrors::Severity expected)
    {
        ++g_checks;
        const SituCpdlcErrors::Severity actual = SituCpdlcErrors::Classify(reply);
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: \"" << reply << "\" - expected " << Name(expected)
                      << ", got " << Name(actual) << "\n";
        }
    }

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition) { ++g_failures; std::cout << "FAIL: " << what << "\n"; }
    }
}

int main()
{
    using namespace SituCpdlcErrors;

    // --- Success. Both forms, and the trailing space that distinguishes an empty peek
    //     from an empty poll must not be mistaken for a failure.
    CheckClass("ok", Ok);
    CheckClass("ok ", Ok);
    CheckClass("ok {CFPAB cpdlc {/data2/1//N/REQUEST LOGON}}", Ok);

    // --- Transient: our own wrapper around a failed HTTP request. Nothing reached
    //     Hoppie, so nothing about the configuration is implicated. This is the case
    //     that used to end a controller's datalink for the session.
    CheckClass("Error: Hoppie poll failed - timed out", Transient);
    CheckClass("Error: Hoppie poll failed - connection reset", Transient);
    CheckClass("Error: Hoppie poll failed - ", Transient);

    // --- Operational: retrying is the correct response, because the lock expires.
    CheckClass("error {callsign already in use}", Operational);
    CheckClass("error (callsign already in use)", Operational);
    CheckClass("ERROR {CALLSIGN ALREADY IN USE}", Operational);

    // --- Fatal: configuration. Polling forever cannot fix any of these.
    CheckClass("error (no logon code)", Fatal);
    CheckClass("error {invalid logon code}", Fatal);
    CheckClass("error {no from address}", Fatal);
    CheckClass("error {logon code not registered with this service}", Fatal);

    // --- An unrecognised error is transient, not fatal. Guessing fatal for a string
    //     nobody has classified yet would reintroduce the defect this file removes, for
    //     every future Hoppie message.
    CheckClass("error {some new thing nobody has seen}", Transient);
    CheckClass("error", Transient);
    CheckClass("", Transient);
    CheckClass("<html>502 Bad Gateway</html>", Transient);

    // --- The relay's reachability banner is an error by Hoppie's format but says
    //     nothing is wrong with us. It must not be fatal, or a stray GET disables CPDLC.
    CheckClass("error {you successfully reached the ACARS system, so your network link "
               "is fine. The rest will depend on your ACARS software}", Transient);

    // --- Bracket form must not matter, since Hoppie uses both and a relay adds its own.
    Check(Classify("error (no logon code)") == Classify("error {no logon code}"),
          "round and curly brackets classify the same");

    // --- Case must not matter.
    Check(Classify("ERROR {NO LOGON CODE}") == Fatal, "upper case is still fatal");

    // --- Nothing throws, including on inputs shorter than the prefixes being compared.
    {
        bool threw = false;
        const char* const nasty[] = { "", "o", "ok", "e", "er", "{", "}", "()" };
        for (const char* raw : nasty)
        {
            try { Classify(raw); Describe(raw, Classify(raw)); }
            catch (...) { threw = true; std::cout << "   threw on: \"" << raw << "\"\n"; }
        }
        Check(!threw, "no input throws");
    }

    // --- The description carries the raw reply, which is the only thing that lets a
    //     controller quote what they saw.
    {
        const std::string reply = "error {callsign already in use}";
        const std::string described = Describe(reply, Operational);
        Check(described.find(reply) != std::string::npos, "operational text quotes the reply");
        Check(described.find("Still trying") != std::string::npos,
              "and says the plugin has not given up");

        const std::string fatal = Describe("error (no logon code)", Fatal);
        Check(fatal.find("stopped") != std::string::npos, "fatal text says it stopped");
    }

    // --- The retry budget is long enough to outlast a network event and short enough to
    //     escape an unclassified permanent fault.
    Check(kTransientFailureLimit >= 5, "at least five minutes of retrying");
    Check(kTransientFailureLimit <= 30, "and not an unbounded loop");

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
