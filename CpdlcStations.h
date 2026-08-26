#pragma once

// Resolving the next controller to the CPDLC station they log on to.
//
// This is what the next data authority uplink needs: given the controller an aircraft is
// going to, which four character Hoppie station should the aircraft be told to log on to,
// and what is that unit called on the radio.
//
// It replaces seven hardcoded if statements that prefix matched controller callsigns,
// were written out twice, and whose second copy tested a variable only assigned in the
// other branch - so it always fell through to an error string.
//
// Two lookups, in order:
//
//   [STATIONS]  LOGIN:<login>:<radio callsign>:<controller id>
//               Exact match on the EuroScope position identifier. Authoritative, and
//               the same table TopSky ships, so a controller can paste theirs over ours.
//
//   [FACILITY]  FACILITY:<callsign prefix>:<login>:<radio callsign>
//               Ours, not TopSky's. Position identifiers are defined by the loaded
//               sector file rather than globally - the same Gander oceanic position is
//               GC in one package and OA in another - so an exhaustive table is only
//               exhaustive against the package it was written for. The prefix, the part
//               of the callsign before the first underscore, is stable across packages.
//
// Measured against the CZQM package: the station table alone resolves 50 of its 69
// centre and flight service positions, and the facility fallback takes it to 69.
//
// Depends only on ConfigFile.h and the standard library. See tests/CpdlcStationsTests.cpp.

#include "ConfigFile.h"

#include <string>
#include <vector>

namespace SituCpdlcStations
{
    struct Station
    {
        std::string login;
        std::string radioCallsign;
        std::string controllerId;
    };

    struct Facility
    {
        std::string callsignPrefix;
        std::string login;
        std::string radioCallsign;
    };

    struct Table
    {
        std::vector<Station> stations;
        std::vector<Facility> facilities;

        // The uplink that tells an aircraft where to log on next. Configurable because
        // which verb this network's clients act on is not settled: Hoppie's own
        // encoding notes document HANDOVER, the real FANS element is NEXT DATA
        // AUTHORITY, and TopSky's binary carries both. The default is what VATCANSitu
        // has always sent. Changing it is a text edit rather than a rebuild, so nobody
        // has to win the argument before the feature works.
        std::string handoverTemplate = "NEXT DATA AUTHORITY @<station>@";

        // Controller identifiers claimed by more than one station. Reported rather than
        // resolved silently: CDQX and CZQX both claim QX in the shipped table, and they
        // are different services - Gander domestic and Gander Radio oceanic.
        std::vector<std::string> duplicateControllerIds;

        std::vector<int> skippedLines;
    };

    // The part of a controller callsign before the first underscore. CZQM_2_CTR is CZQM,
    // MTL_AY_CTR is MTL.
    inline std::string CallsignPrefix(const std::string& callsign)
    {
        const size_t underscore = callsign.find('_');
        return (underscore == std::string::npos) ? callsign : callsign.substr(0, underscore);
    }

    inline Table Parse(const SituConfig::ParseResult& parsed)
    {
        Table table;
        table.skippedLines = parsed.skippedLines;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (record.isAssignment)
            {
                if (record.key == "CPDLC_HandoverTemplate") { table.handoverTemplate = record.value; }
                // Other assignments belong to sections this does not own - the PDC
                // header, for one - and are not this parser's business to complain about.
                continue;
            }

            if (record.type == "LOGIN")
            {
                if (record.fields.size() < 3 || record.fields[0].empty() || record.fields[2].empty())
                {
                    table.skippedLines.push_back(record.line);
                    continue;
                }
                Station station;
                station.login = record.fields[0];
                station.radioCallsign = record.fields[1];
                station.controllerId = record.fields[2];
                table.stations.push_back(station);
                continue;
            }

            if (record.type == "FACILITY")
            {
                if (record.fields.size() < 3 || record.fields[0].empty() || record.fields[1].empty())
                {
                    table.skippedLines.push_back(record.line);
                    continue;
                }
                Facility facility;
                facility.callsignPrefix = record.fields[0];
                facility.login = record.fields[1];
                facility.radioCallsign = record.fields[2];
                table.facilities.push_back(facility);
                continue;
            }

            // FREETEXT, DCL and anything else in the file belong to other readers.
        }

        // Note identifiers claimed by more than one station, once each.
        for (size_t i = 0; i < table.stations.size(); ++i)
        {
            bool duplicate = false;
            for (size_t j = 0; j < i; ++j)
            {
                if (table.stations[j].controllerId == table.stations[i].controllerId) { duplicate = true; break; }
            }
            if (!duplicate) { continue; }

            bool alreadyNoted = false;
            for (const std::string& noted : table.duplicateControllerIds)
            {
                if (noted == table.stations[i].controllerId) { alreadyNoted = true; break; }
            }
            if (!alreadyNoted) { table.duplicateControllerIds.push_back(table.stations[i].controllerId); }
        }

        return table;
    }

    struct Resolution
    {
        bool found = false;
        std::string login;
        std::string radioCallsign;

        // True when the station table had nothing and the facility fallback answered.
        // Worth surfacing: it usually means the controller is running a different sector
        // package from the one this table was written against.
        bool viaFacility = false;
    };

    // positionId is what GetHandoffTargetControllerId or GetTrackingControllerId returns;
    // controllerCallsign is the full callsign, used to disambiguate and as the fallback.
    inline Resolution Resolve(const Table& table,
                              const std::string& positionId,
                              const std::string& controllerCallsign)
    {
        Resolution resolution;
        const std::string prefix = CallsignPrefix(controllerCallsign);

        const Station* firstMatch = nullptr;
        const Station* prefixMatch = nullptr;

        for (const Station& station : table.stations)
        {
            if (positionId.empty() || station.controllerId != positionId) { continue; }

            if (firstMatch == nullptr) { firstMatch = &station; }

            // Two stations can claim one identifier. Prefer the one whose login matches
            // the callsign prefix, which picks Gander domestic for a CZQX_* position and
            // leaves Gander Radio for whoever logs on as CDQX - rather than taking
            // whichever row happened to be read first.
            if (prefixMatch == nullptr && !prefix.empty() && station.login == prefix) { prefixMatch = &station; }
        }

        const Station* chosen = (prefixMatch != nullptr) ? prefixMatch : firstMatch;
        if (chosen != nullptr)
        {
            resolution.found = true;
            resolution.login = chosen->login;
            resolution.radioCallsign = chosen->radioCallsign;
            return resolution;
        }

        for (const Facility& facility : table.facilities)
        {
            if (facility.callsignPrefix != prefix) { continue; }

            resolution.found = true;
            resolution.login = facility.login;
            resolution.radioCallsign = facility.radioCallsign;
            resolution.viaFacility = true;
            return resolution;
        }

        return resolution;
    }

    // Substitutes <station> and <radio> into the configured template. Anything the
    // template does not mention is simply not inserted, so a site that wants a bare
    // "HANDOVER @CZQX@" and one that wants the radio callsign spelled out both work.
    inline std::string FormatHandover(const Table& table, const Resolution& resolution)
    {
        std::string out = table.handoverTemplate;

        struct Substitution { const char* token; const std::string& value; };
        const Substitution substitutions[] = {
            { "<station>", resolution.login },
            { "<radio>", resolution.radioCallsign },
        };

        for (const Substitution& substitution : substitutions)
        {
            const std::string token(substitution.token);
            size_t at = out.find(token);
            while (at != std::string::npos)
            {
                out = out.substr(0, at) + substitution.value + out.substr(at + token.size());
                at = out.find(token, at + substitution.value.size());
            }
        }
        return out;
    }
}
