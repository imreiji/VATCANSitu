// Tests for TbsConfig.h.
//
// The important property is not that the parser works but that the shipped file
// reproduces what the compiled table did. The old matrix is written out again here, in
// the form it had in CSiTRadar.cpp, and every leader/follower pair is run through both.
// A divergence is a defect regardless of which side looks nicer.
//
//   cl /EHsc /W4 /std:c++17 tests\TbsConfigTests.cpp /Fe:TbsConfigTests.exe
//   TbsConfigTests.exe

#include "../TbsConfig.h"

#include <cmath>
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

    void CheckNear(double actual, double expected, const std::string& what)
    {
        ++g_checks;
        if (std::fabs(actual - expected) > 1e-9)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected " << expected
                      << ", got " << actual << "\n";
        }
    }

    // The compiled table, reproduced from CSiTRadar.cpp before the change. Returns 0
    // for a pair it had no rule for, which is what left tbsDist at zero and drew
    // nothing.
    double OldTable(char leader, int followerIndex, double gs)
    {
        double tbsDist = 0;

        if (leader == 'L')
        {
            tbsDist = 3;
            if (gs / 3600 * 68 < tbsDist) { tbsDist = gs / 3600 * 68; }
        }
        if (leader == 'M')
        {
            if (followerIndex == 0)
            {
                tbsDist = 4;
                if (gs / 3600 * 90 < tbsDist) { tbsDist = gs / 3600 * 90; }
            }
            else if (followerIndex >= 1)
            {
                tbsDist = 3;
                if (gs / 3600 * 68 < tbsDist) { tbsDist = gs / 3600 * 68; }
            }
        }
        if (leader == 'H')
        {
            if (followerIndex == 0)      { tbsDist = 6; if (gs / 3600 * 135 < tbsDist) { tbsDist = gs / 3600 * 135; } }
            else if (followerIndex == 1) { tbsDist = 5; if (gs / 3600 * 113 < tbsDist) { tbsDist = gs / 3600 * 113; } }
            else if (followerIndex == 2) { tbsDist = 4; if (gs / 3600 * 90  < tbsDist) { tbsDist = gs / 3600 * 90; } }
            else if (followerIndex == 3) { tbsDist = 3; if (gs / 3600 * 68  < tbsDist) { tbsDist = gs / 3600 * 68; } }
        }
        if (leader == 'J')
        {
            if (followerIndex == 0)      { tbsDist = 8; if (gs / 3600 * 180 < tbsDist) { tbsDist = gs / 3600 * 180; } }
            else if (followerIndex == 1) { tbsDist = 7; if (gs / 3600 * 158 < tbsDist) { tbsDist = gs / 3600 * 158; } }
            else if (followerIndex == 2) { tbsDist = 6; if (gs / 3600 * 135 < tbsDist) { tbsDist = gs / 3600 * 135; } }
            else if (followerIndex == 3) { tbsDist = 4; if (gs / 3600 * 90  < tbsDist) { tbsDist = gs / 3600 * 90; } }
        }
        return tbsDist;
    }

    std::string ReadFile(const char* path)
    {
        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }
}

int main(int argc, char** argv)
{
    using namespace SituTbs;

    const char* path = (argc > 1) ? argv[1] : "SituTBS.txt";
    const std::string text = ReadFile(path);
    Check(!text.empty(), std::string("the shipped file was found and is not empty: ") + path);
    if (text.empty())
    {
        std::cout << "cannot continue without the file\n";
        return 1;
    }

    const SituConfig::ParseResult parsed = SituConfig::Parse(text);
    Check(parsed.skippedLines.empty(), "the shipped file tokenises with nothing skipped");

    const Config config = Parse(parsed);
    Check(config.rejectedLines.empty(), "the shipped file has no rejected lines");
    Check(config.separation.size() == 11, "eleven separation rules were read");
    Check(config.airports.size() == 1, "one airport was read");

    // --- The gate values that used to be bare numbers in the draw.
    CheckNear(config.gate.minDistanceNm, 1.0, "min distance");
    CheckNear(config.gate.maxDistanceNm, 20.0, "max distance");
    CheckNear(config.gate.minAltitudeFt, 500.0, "min altitude");
    CheckNear(config.gate.headingToleranceDeg, 7.0, "heading tolerance");
    CheckNear(config.gate.mixedModeMinimumNm, 5.0, "mixed mode minimum");

    // --- The airport list is opt-in and carries nothing else. The magnetic variation
    //     that used to live here is gone: the course now comes from the runway's own
    //     thresholds, which is true already.
    {
        Check(FindAirport(config, "CYYZ") != nullptr, "CYYZ is configured");
        Check(FindAirport(config, "CYOW") == nullptr, "an unlisted airport has no TBS");
    }

    // --- A file written against the old two field form keeps its airports rather than
    //     losing them, since the second field is simply ignored now.
    {
        const Config legacy = Parse(SituConfig::Parse("[AIRPORTS]\nAIRPORT:CYYZ:10\nAIRPORT:CYOW\n"));
        Check(legacy.airports.size() == 2, "both forms are accepted");
        Check(legacy.rejectedLines.empty(), "and neither is reported as bad");
    }

    // --- The whole matrix against the compiled table it replaces, over a range of
    //     groundspeeds that crosses where the time based figure overtakes the fixed one.
    {
        int compared = 0;
        int diverged = 0;
        const char leaders[] = { 'L', 'M', 'H', 'J' };

        for (char leader : leaders)
        {
            for (int followerIndex = 0; followerIndex <= 3; ++followerIndex)
            {
                for (int gs = 0; gs <= 400; gs += 5)
                {
                    const double expected = OldTable(leader, followerIndex, gs);

                    double actual = 0.0;
                    const bool found = SeparationFor(config, leader,
                                                     WtcForFollowerIndex(followerIndex),
                                                     gs, false, actual);
                    if (!found) { actual = 0.0; }

                    ++compared;
                    if (std::fabs(actual - expected) > 1e-9)
                    {
                        ++diverged;
                        if (diverged <= 3)
                        {
                            std::cout << "   diverged: leader " << leader
                                      << " follower " << followerIndex
                                      << " gs " << gs
                                      << " old " << expected << " new " << actual << "\n";
                        }
                    }
                }
            }
        }
        Check(diverged == 0,
              "the file reproduces the compiled table exactly over "
              + std::to_string(compared) + " leader, follower and groundspeed combinations");
        std::cout << "matrix: " << compared << " combinations compared, "
                  << diverged << " divergences\n";
    }

    // --- Time based behaviour: slow down and the distance shrinks, but never past the
    //     fixed figure.
    {
        double fast = 0.0, slow = 0.0;
        SeparationFor(config, 'J', 'L', 300.0, false, fast);
        SeparationFor(config, 'J', 'L', 100.0, false, slow);
        Check(slow < fast, "a slower follower is spaced closer in distance");
        CheckNear(fast, 8.0, "at 300kt the fixed 8nm is the lesser figure");
        CheckNear(slow, 100.0 / 3600.0 * 180.0, "at 100kt the time figure governs");
    }

    // --- Mixed mode floors the result.
    {
        double normal = 0.0, mixed = 0.0;
        SeparationFor(config, 'L', 'L', 120.0, false, normal);
        SeparationFor(config, 'L', 'L', 120.0, true, mixed);
        Check(normal < 5.0, "light behind light is under the mixed mode floor normally");
        CheckNear(mixed, 5.0, "mixed mode raises it to the floor");
    }

    // --- Mixed mode floors a rule that matched, and must not manufacture one for a
    //     pair the matrix does not cover. The old code applied the floor after the
    //     matrix unconditionally, so an unrecognised wake category left with five miles.
    {
        double unused = 0.0;
        Check(!SeparationFor(config, 'X', 'M', 150.0, true, unused),
              "mixed mode does not invent a separation for an unknown leader");
    }

    // --- At rest nothing is drawn.
    {
        double unused = 0.0;
        Check(!SeparationFor(config, 'M', 'M', 0.0, false, unused), "no groundspeed, no marker");
        Check(!SeparationFor(config, 'M', 'M', 0.0, true, unused), "and mixed mode does not override that");
    }

    // --- A pair with no rule draws nothing rather than guessing.
    {
        double unused = 0.0;
        Check(!SeparationFor(config, 'X', 'M', 150.0, false, unused), "unknown leader has no rule");
        Check(!SeparationFor(config, '?', '?', 150.0, false, unused), "unknown pair has no rule");
    }

    // --- First match wins, which is what lets a specific row precede a wildcard.
    {
        const Config ordered = Parse(SituConfig::Parse(
            "[SEPARATION]\nSEP:M:L:4:90\nSEP:M:*:3:68\n"));
        double specific = 0.0, wildcard = 0.0;
        SeparationFor(ordered, 'M', 'L', 400.0, false, specific);
        SeparationFor(ordered, 'M', 'H', 400.0, false, wildcard);
        CheckNear(specific, 4.0, "the specific row wins for a light follower");
        CheckNear(wildcard, 3.0, "the wildcard row covers the rest");
    }

    // --- A bad line costs that line and not the file.
    {
        const Config damaged = Parse(SituConfig::Parse(
            "[SEPARATION]\nSEP:M:L:four:90\nSEP:M:*:3:68\nSEP:H:H:0:90\nMinAltitude=abc\n"));
        Check(damaged.separation.size() == 1, "only the sound rule survived");
        Check(damaged.rejectedLines.size() == 3,
              "three bad lines were reported, not dropped (got "
              + std::to_string(damaged.rejectedLines.size()) + ")");
        CheckNear(damaged.gate.minAltitudeFt, 500.0, "an unparseable value leaves the default");
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
