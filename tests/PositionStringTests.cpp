// Tests for PositionString.h.
//
// The old formatter is reproduced verbatim below, exactly as it stood inline in
// CSiTRadar::OnClickScreenObject, float types included, so the two can be run against the
// same positions and compared. It is here as a record of what the bugs were, not as a
// reference to match: unlike the scratchpad, the old output is not a format anyone should
// want preserved.
//
// Three defects, all of which reached the network through SetRoute / AmendFlightPlan:
//
//   1. Only the longitude degrees were zero padded. Latitude degrees and both minute
//      fields were not, so 45 degrees 05 minutes N, 066 degrees 12 minutes W came out as
//      "455N06612W" - the wrong width, and unreadable as the position that was meant.
//
//   2. Minutes were rounded with no carry, so a latitude just under a whole degree
//      produced sixty minutes: "4560N".
//
//   3. modf was handed a float* with a double first argument, so overload resolution
//      picked the float overload and narrowed the coordinate before splitting it.
//
// Standalone: PositionString.h depends only on <string> and <cmath>.
//
//   cl /EHsc /W4 /std:c++17 tests\PositionStringTests.cpp /Fe:PositionStringTests.exe
//   PositionStringTests.exe

#include "../PositionString.h"

#include <cmath>
#include <cstdlib>
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

    // Verbatim from CSiTRadar.cpp before the extraction, with one change forced on it.
    //
    // The original wrote modf(longitude, &lon) with lon declared float. MSVC accepts that
    // by picking the float overload and narrowing the first argument - which is exactly
    // what warning C4244 was reporting, and exactly the third bug. Other compilers reject
    // the call outright, so the narrowing is spelled out here with modff to keep this
    // reference buildable while computing precisely what MSVC computed.
    std::string OldFormat(double latitude, double longitude)
    {
        std::string pposStr;
        float lat, lon, latmin, lonmin;
        double longitudedecmin = modff(static_cast<float>(longitude), &lon);
        double latitudedecmin = modff(static_cast<float>(latitude), &lat);

        latmin = static_cast<float>(std::fabs(round(latitudedecmin * 60)));
        lonmin = static_cast<float>(std::fabs(round(longitudedecmin * 60)));
        std::string lonstring = std::to_string(static_cast<int>(std::fabs(lon)));
        if (lonstring.size() < 3)
        {
            lonstring.insert(lonstring.begin(), 3 - lonstring.size(), '0');
        }

        if (lon < 0)
        {
            if (lat > 0)
            {
                pposStr = std::to_string(static_cast<int>(lat)) + std::to_string(static_cast<int>(latmin)) + "N" + lonstring + std::to_string(static_cast<int>(lonmin)) + "W";
            }
            else
            {
                pposStr = std::to_string(static_cast<int>(std::fabs(lat))) + std::to_string(static_cast<int>(latmin)) + "S" + lonstring + std::to_string(static_cast<int>(lonmin)) + "W";
            }
        }
        else
        {
            if (lat > 0)
            {
                pposStr = std::to_string(static_cast<int>(lat)) + std::to_string(static_cast<int>(latmin)) + "N" + lonstring + std::to_string(static_cast<int>(lonmin)) + "E";
            }
            else
            {
                pposStr = std::to_string(static_cast<int>(std::fabs(lat))) + std::to_string(static_cast<int>(latmin)) + "S" + lonstring + std::to_string(static_cast<int>(lonmin)) + "E";
            }
        }
        return pposStr;
    }

    // DDMM[NS]DDDMM[EW]: eleven characters, digits everywhere but the two hemispheres,
    // and neither minute field may reach sixty.
    bool WellFormed(const std::string& s)
    {
        if (s.size() != 11) { return false; }
        for (size_t i = 0; i < s.size(); ++i)
        {
            const char c = s[i];
            if (i == 4)
            {
                if (c != 'N' && c != 'S') { return false; }
            }
            else if (i == 10)
            {
                if (c != 'E' && c != 'W') { return false; }
            }
            else if (c < '0' || c > '9')
            {
                return false;
            }
        }
        return std::stoi(s.substr(2, 2)) < 60 && std::stoi(s.substr(8, 2)) < 60;
    }
}

int main()
{
    using namespace SituPosition;

    // --- The four quadrants, with values needing no padding and no carry.
    CheckEqual(FormatPositionString(45.5, -66.5),  "4530N06630W", "NW quadrant");
    CheckEqual(FormatPositionString(-45.5, -66.5), "4530S06630W", "SW quadrant");
    CheckEqual(FormatPositionString(45.5, 66.5),   "4530N06630E", "NE quadrant");
    CheckEqual(FormatPositionString(-45.5, 66.5),  "4530S06630E", "SE quadrant");

    // --- Bug 1: every field padded to its documented width.
    CheckEqual(FormatPositionString(45.0 + 5.0 / 60.0, -(66.0 + 12.0 / 60.0)),
               "4505N06612W", "single digit latitude minutes are padded");
    CheckEqual(FormatPositionString(5.0 + 5.0 / 60.0, -(6.0 + 6.0 / 60.0)),
               "0505N00606W", "single digit degrees and minutes are all padded");
    CheckEqual(FormatPositionString(45.5, -6.5), "4530N00630W", "longitude degrees pad to three");

    // --- Bug 2: minutes rounding up to sixty carry into the degrees.
    CheckEqual(FormatPositionString(45.99999, -66.99999), "4600N06700W", "59.9994 minutes carries");
    CheckEqual(FormatPositionString(-45.99999, 66.99999), "4600S06700E", "the carry works in every quadrant");

    // --- The equator and the prime meridian resolve to N and E, as the sign test did.
    CheckEqual(FormatPositionString(0.0, 0.0), "0000N00000E", "zero is north and east");

    // --- Bug 3: precision. A float carries about seven significant digits, so a
    //     coordinate narrowed before it is split can land on the wrong side of a minute
    //     boundary and come out one arcminute off.
    //
    //     This only bites within about half an ULP of a rounding boundary, so a coarse
    //     sweep finds nothing and would let the claim through untested. Sample where it
    //     actually happens - just either side of a whole half minute - and separately
    //     report the rate over uniform latitudes, which is the number that says how often
    //     this reached the network.
    {
        int boundaryDivergences = 0;
        for (int degrees = 1; degrees <= 89; ++degrees)
        {
            for (int minutes = 0; minutes < 60; ++minutes)
            {
                for (int step = -200; step <= 200; ++step)
                {
                    const double lat = degrees + (minutes + 0.5) / 60.0 + step * 1e-9;
                    const double narrowed = static_cast<double>(static_cast<float>(lat));

                    int wideDeg = 0, wideMin = 0, narrowDeg = 0, narrowMin = 0;
                    SplitDegreesMinutes(lat, wideDeg, wideMin);
                    SplitDegreesMinutes(narrowed, narrowDeg, narrowMin);

                    if (wideDeg != narrowDeg || wideMin != narrowMin) { ++boundaryDivergences; }
                }
            }
        }
        Check(boundaryDivergences > 0,
              "narrowing to float changes the split minute near a rounding boundary (found "
              + std::to_string(boundaryDivergences) + ")");

        int uniformDivergences = 0;
        const int uniformSamples = 2000000;
        for (int i = 0; i < uniformSamples; ++i)
        {
            const double lat = -90.0 + (i * 180.0) / uniformSamples;
            const double narrowed = static_cast<double>(static_cast<float>(lat));

            int wideDeg = 0, wideMin = 0, narrowDeg = 0, narrowMin = 0;
            SplitDegreesMinutes(lat, wideDeg, wideMin);
            SplitDegreesMinutes(narrowed, narrowDeg, narrowMin);

            if (wideDeg != narrowDeg || wideMin != narrowMin) { ++uniformDivergences; }
        }
        std::cout << "precision: " << boundaryDivergences
                  << " divergences near rounding boundaries; " << uniformDivergences
                  << " in " << uniformSamples << " uniform latitudes\n";
    }

    // --- Every output is well formed over a wide sweep. This is the property the old
    //     code could not hold.
    {
        int oldMalformed = 0;
        int newMalformed = 0;
        for (int latTenths = -900; latTenths <= 900; latTenths += 7)
        {
            for (int lonTenths = -1800; lonTenths <= 1800; lonTenths += 13)
            {
                const double lat = latTenths / 10.0;
                const double lon = lonTenths / 10.0;
                if (!WellFormed(FormatPositionString(lat, lon))) { ++newMalformed; }
                if (!WellFormed(OldFormat(lat, lon))) { ++oldMalformed; }
            }
        }
        Check(newMalformed == 0,
              "every formatted position is well formed (malformed: " + std::to_string(newMalformed) + ")");
        Check(oldMalformed > 0,
              "the old formatter produced malformed positions, which is the bug being fixed (malformed: "
              + std::to_string(oldMalformed) + ")");
        std::cout << "sweep: old produced " << oldMalformed
                  << " malformed positions, new produced " << newMalformed << "\n";
    }

    // --- Nothing on a fine global sweep is malformed.
    {
        int malformed = 0;
        for (int i = 0; i < 500000; ++i)
        {
            const double lat = -90.0 + (i * 180.0) / 500000.0;
            const double lon = -180.0 + (i * 360.0) / 500000.0;
            if (!WellFormed(FormatPositionString(lat, lon))) { ++malformed; }
        }
        Check(malformed == 0, "no position on a fine global sweep is malformed");
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
