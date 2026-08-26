#pragma once

// Time Based Separation configuration, read from SituTBS.txt.
//
// TBS draws a marker on final approach showing where the following aircraft should be,
// so spacing can be judged by eye rather than arithmetic. The separation is the lesser
// of a fixed distance and a time converted to distance at the current groundspeed -
// which is the point of it: into a headwind the aircraft is slower, so the distance
// shrinks while the time behind the leader stays constant, and the landing rate that
// fixed distance spacing throws away is recovered.
//
// All of it used to be compiled in, gated on the destination being CYYZ, with Toronto's
// magnetic variation written as a bare 10 in two places with opposite signs. None of
// that is a preference or a standard - it is local data about one airport - and while
// it was in the source nobody could audit the separation figures against the AIP
// without reading C++.
//
// Depends only on ConfigFile.h and the standard library. See tests/TbsConfigTests.cpp.

#include "ConfigFile.h"

#include <string>
#include <vector>

namespace SituTbs
{
    struct Airport
    {
        std::string icao;

        // Degrees to add to a magnetic heading to get a true one. EuroScope reports
        // track in true and the runway heading is set in magnetic, so this is what
        // reconciles them. West variation is negative.
        double magneticVariation = 0.0;
    };

    // When the marker is drawn at all. These were bare numbers inside the draw.
    struct Gate
    {
        double minDistanceNm = 1.0;
        double maxDistanceNm = 20.0;
        double minAltitudeFt = 500.0;

        // How far off the runway heading a track may be and still count as established.
        double headingToleranceDeg = 7.0;

        // Floor applied on a mixed mode runway, where departures share the strip.
        double mixedModeMinimumNm = 5.0;
    };

    // One row of the wake matrix. leader and follower are ICAO wake categories - L, M,
    // H, J - or '*' for any. Rows are tried in file order and the first match wins, so
    // a specific row must precede the wildcard that would also match it.
    struct SeparationRule
    {
        char leader = '*';
        char follower = '*';
        double distanceNm = 0.0;
        double timeSeconds = 0.0;
    };

    struct Config
    {
        std::vector<Airport> airports;
        Gate gate;
        std::vector<SeparationRule> separation;

        // Lines that parsed but carried something unusable. Reported at load rather
        // than dropped, because a separation figure that silently failed to load is the
        // worst thing in this file to be quiet about.
        std::vector<int> rejectedLines;
    };

    // The follower's category is held as an index by the tag toggle: 0 light, 1 medium,
    // 2 heavy, 3 super. This is the one place that mapping is written down.
    inline char WtcForFollowerIndex(int index)
    {
        switch (index)
        {
        case 0:  return 'L';
        case 1:  return 'M';
        case 2:  return 'H';
        case 3:  return 'J';
        default: return '*';
        }
    }

    inline char UpperWtc(char c)
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }

    inline bool IsWtc(char c)
    {
        return c == 'L' || c == 'M' || c == 'H' || c == 'J' || c == '*';
    }

    // Returns false rather than throwing on anything unparseable, so one bad line costs
    // that line and not the file.
    inline bool ToDouble(const std::string& text, double& out)
    {
        if (text.empty()) { return false; }

        size_t index = 0;
        bool negative = false;
        if (text[0] == '-' || text[0] == '+') { negative = (text[0] == '-'); index = 1; }
        if (index >= text.size()) { return false; }

        double whole = 0.0;
        bool anyDigit = false;
        for (; index < text.size() && text[index] != '.'; ++index)
        {
            if (text[index] < '0' || text[index] > '9') { return false; }
            whole = whole * 10.0 + (text[index] - '0');
            anyDigit = true;
        }
        if (index < text.size() && text[index] == '.')
        {
            double scale = 0.1;
            for (++index; index < text.size(); ++index)
            {
                if (text[index] < '0' || text[index] > '9') { return false; }
                whole += (text[index] - '0') * scale;
                scale *= 0.1;
                anyDigit = true;
            }
        }
        if (!anyDigit) { return false; }

        out = negative ? -whole : whole;
        return true;
    }

    inline Config Parse(const SituConfig::ParseResult& parsed)
    {
        Config config;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (record.isAssignment)
            {
                double value = 0.0;
                if (!ToDouble(record.value, value)) { config.rejectedLines.push_back(record.line); continue; }

                if (record.key == "MinDistance")           { config.gate.minDistanceNm = value; }
                else if (record.key == "MaxDistance")       { config.gate.maxDistanceNm = value; }
                else if (record.key == "MinAltitude")       { config.gate.minAltitudeFt = value; }
                else if (record.key == "HeadingTolerance")  { config.gate.headingToleranceDeg = value; }
                else if (record.key == "MixedModeMinimum")  { config.gate.mixedModeMinimumNm = value; }
                else                                        { config.rejectedLines.push_back(record.line); }
                continue;
            }

            if (record.type == "AIRPORT")
            {
                // AIRPORT:<icao>:<magnetic variation>
                if (record.fields.size() < 2) { config.rejectedLines.push_back(record.line); continue; }

                Airport airport;
                airport.icao = record.fields[0];
                if (airport.icao.empty() || !ToDouble(record.fields[1], airport.magneticVariation))
                {
                    config.rejectedLines.push_back(record.line);
                    continue;
                }
                config.airports.push_back(airport);
                continue;
            }

            if (record.type == "SEP")
            {
                // SEP:<leader>:<follower>:<distance nm>:<time seconds>
                if (record.fields.size() < 4) { config.rejectedLines.push_back(record.line); continue; }

                SeparationRule rule;
                if (record.fields[0].size() != 1 || record.fields[1].size() != 1)
                {
                    config.rejectedLines.push_back(record.line);
                    continue;
                }
                rule.leader = UpperWtc(record.fields[0][0]);
                rule.follower = UpperWtc(record.fields[1][0]);

                if (!IsWtc(rule.leader) || !IsWtc(rule.follower)
                    || !ToDouble(record.fields[2], rule.distanceNm)
                    || !ToDouble(record.fields[3], rule.timeSeconds)
                    || rule.distanceNm <= 0.0 || rule.timeSeconds <= 0.0)
                {
                    config.rejectedLines.push_back(record.line);
                    continue;
                }
                config.separation.push_back(rule);
                continue;
            }

            config.rejectedLines.push_back(record.line);
        }

        return config;
    }

    inline const Airport* FindAirport(const Config& config, const std::string& icao)
    {
        for (const Airport& airport : config.airports)
        {
            if (airport.icao == icao) { return &airport; }
        }
        return nullptr;
    }

    // The separation to draw, in nautical miles. False when no rule covers the pair, in
    // which case nothing should be drawn rather than something guessed.
    inline bool SeparationFor(const Config& config,
                              char leader,
                              char follower,
                              double groundspeedKts,
                              bool mixedMode,
                              double& outNm)
    {
        leader = UpperWtc(leader);
        follower = UpperWtc(follower);

        // No groundspeed, no marker. The old code reached the same outcome by accident:
        // it took the lesser of the fixed distance and gs/3600*seconds, which is zero at
        // rest, and the caller then skipped drawing because the distance was zero. Said
        // plainly here instead, because a separation of zero is not a separation.
        if (groundspeedKts <= 0.0) { return false; }

        for (const SeparationRule& rule : config.separation)
        {
            if (rule.leader != '*' && rule.leader != leader) { continue; }
            if (rule.follower != '*' && rule.follower != follower) { continue; }

            double distance = rule.distanceNm;

            // The time based part: the same seconds behind the leader, expressed as the
            // distance the follower covers at its present groundspeed. Taken only when
            // it is the lesser of the two, so the fixed distance remains a ceiling.
            const double timeDistance = groundspeedKts / 3600.0 * rule.timeSeconds;
            if (timeDistance < distance) { distance = timeDistance; }

            // The mixed mode floor applies only once a rule has matched. It used to be
            // applied after the matrix regardless, so a pair the matrix had no rule for
            // came out of it with a distance of zero and left with five - inventing a
            // separation for an aircraft whose wake category was not recognised.
            if (mixedMode && distance < config.gate.mixedModeMinimumNm)
            {
                distance = config.gate.mixedModeMinimumNm;
            }

            outNm = distance;
            return true;
        }
        return false;
    }
}
