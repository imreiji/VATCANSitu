// Tests for TagCallsign.h.
//
//   cl /EHsc /W4 /std:c++17 tests\TagCallsignTests.cpp /Fe:TagCallsignTests.exe
//   TagCallsignTests.exe

#include "../TagCallsign.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void CheckEqual(const std::string& in, const std::string& expected)
    {
        ++g_checks;
        const std::string actual = SituTag::DisplayCallsign(in);
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: \"" << in << "\" -> expected \"" << expected
                      << "\", got \"" << actual << "\"\n";
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
    using namespace SituTag;

    // --- The three prefixes that shorten.
    CheckEqual("CGABC", "GABC");
    CheckEqual("CFABC", "FABC");
    CheckEqual("CIABC", "IABC");
    CheckEqual("CGZZZ", "GZZZ");
    CheckEqual("CFAAA", "FAAA");

    // --- The trailing three must all be letters. A registration with a digit in it is
    //     not one of these, and shortening it would put a callsign on the tag that is
    //     not the aircraft's.
    CheckEqual("CGAB1", "CGAB1");
    CheckEqual("CG1BC", "CG1BC");
    CheckEqual("CGA1C", "CGA1C");
    CheckEqual("CG123", "CG123");

    // --- Length is exact. Four characters is an ICAO code, six is something else.
    CheckEqual("CGAB", "CGAB");
    CheckEqual("CGABCD", "CGABCD");
    CheckEqual("CZQM", "CZQM");
    CheckEqual("C", "C");
    CheckEqual("", "");

    // --- Only G, F and I. Other second letters are left alone rather than guessed at.
    CheckEqual("CJABC", "CJABC");
    CheckEqual("CAABC", "CAABC");
    CheckEqual("CZABC", "CZABC");

    // --- Must start with C. A five letter callsign that merely ends the right way is
    //     not a Canadian registration.
    CheckEqual("XGABC", "XGABC");
    CheckEqual("AGABC", "AGABC");

    // --- Real airline callsigns are untouched. These are the ones a false positive
    //     would corrupt, so they are checked explicitly rather than assumed.
    CheckEqual("ACA123", "ACA123");
    CheckEqual("WJA456", "WJA456");
    CheckEqual("JZA8123", "JZA8123");
    CheckEqual("N123AB", "N123AB");
    CheckEqual("GABC", "GABC");

    // --- Lower case is not shortened. Callsigns arrive upper case from the network, and
    //     accepting mixed case would mean deciding what to do about the C as well.
    CheckEqual("cgabc", "cgabc");
    CheckEqual("CGabc", "CGabc");

    // --- The predicate agrees with the formatter.
    Check(IsCanadianRegistration("CGABC"), "CGABC is a registration");
    Check(!IsCanadianRegistration("ACA123"), "an airline callsign is not");
    Check(!IsCanadianRegistration(""), "an empty callsign is not");

    // --- Shortening removes exactly one character and keeps the rest verbatim.
    {
        const std::string in = "CFXYZ";
        const std::string out = DisplayCallsign(in);
        Check(out.size() == in.size() - 1, "one character shorter");
        Check(in.compare(1, 4, out) == 0, "and the remainder is untouched");
    }

    // --- Idempotent: a shortened callsign is not shortened again. GABC is four
    //     characters so it cannot match, but this pins the property rather than
    //     relying on that.
    Check(DisplayCallsign(DisplayCallsign("CGABC")) == "GABC", "shortening twice is the same as once");

    // --- Nothing throws.
    {
        bool threw = false;
        const char* const nasty[] = { "", "C", "CG", "CGA", "CGAB", "     ", "C-GABC" };
        for (const char* raw : nasty)
        {
            try { DisplayCallsign(raw); IsCanadianRegistration(raw); }
            catch (...) { threw = true; std::cout << "   threw on: \"" << raw << "\"\n"; }
        }
        Check(!threw, "no input throws");
    }

    // --- A dashed registration is not shortened. The network strips the dash; if one
    //     ever arrives with it, leaving it alone is safer than assuming the format.
    CheckEqual("C-GABC", "C-GABC");

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
