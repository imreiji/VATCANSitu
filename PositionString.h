#pragma once

// Formats a latitude/longitude as the degrees-and-minutes position string used in an
// ICAO route: DDMM[N|S]DDDMM[E|W], for example 4530N06612W.
//
// This is prepended to the filed route and pushed to the network with AmendFlightPlan,
// so it is read by every other controller and by the pilot's client. It has to be
// exactly the documented width - two digits of latitude degrees, three of longitude,
// two of minutes each - or it is not a position at all.
//
// Depends on nothing but <string> and <cmath> so it can be tested without MFC or the
// EuroScope SDK. See tests/PositionStringTests.cpp.

#include <cmath>
#include <string>

namespace SituPosition
{
    // Left-pads with zeroes to the given width. Values wider than the field are returned
    // unchanged rather than truncated: a wrong number is better than a plausible one.
    inline std::string PadLeft(int value, size_t width)
    {
        std::string s = std::to_string(value);
        if (s.size() < width) { s.insert(s.begin(), width - s.size(), '0'); }
        return s;
    }

    // Splits a signed decimal degree into whole degrees and whole minutes, both positive.
    //
    // Rounding the minutes can carry: 45.99999 degrees is 59.9994 minutes, which rounds
    // to 60, and "4560" is not a valid position. The carry moves into the degrees, the
    // way it would if this were done by hand.
    inline void SplitDegreesMinutes(double degrees, int& outDegrees, int& outMinutes)
    {
        const double magnitude = std::fabs(degrees);

        double whole = 0.0;
        const double fraction = std::modf(magnitude, &whole);

        outDegrees = static_cast<int>(whole);
        outMinutes = static_cast<int>(std::lround(fraction * 60.0));

        if (outMinutes >= 60)
        {
            outMinutes -= 60;
            outDegrees += 1;
        }
    }

    inline std::string FormatPositionString(double latitude, double longitude)
    {
        int latDegrees = 0, latMinutes = 0;
        int lonDegrees = 0, lonMinutes = 0;
        SplitDegreesMinutes(latitude, latDegrees, latMinutes);
        SplitDegreesMinutes(longitude, lonDegrees, lonMinutes);

        // Zero, north and east. A position exactly on the equator or the prime meridian
        // has to pick one, and this matches how the sign test read before.
        const char latHemisphere = (latitude < 0.0) ? 'S' : 'N';
        const char lonHemisphere = (longitude < 0.0) ? 'W' : 'E';

        return PadLeft(latDegrees, 2) + PadLeft(latMinutes, 2) + latHemisphere
             + PadLeft(lonDegrees, 3) + PadLeft(lonMinutes, 2) + lonHemisphere;
    }
}
