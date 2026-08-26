#pragma once

// Reads a controller's self-declared CPDLC station out of their VATSIM info block.
//
// Controllers advertise datalink in free text, in the "text_atis" lines of the VATSIM
// datafeed. There is no standard for it. Real examples, taken from the network:
//
//     CPDLC LOGON HSSS
//     CPDLC LOGON LPYP.
//     CPDLC LOGON FL300+ [OIIX]
//     CPDLC/DCL [OEJD]
//     CPDLC Available on SBAZ
//     CPDLC [OOM1]
//     CPDLC MWS2
//     CPDLC/PDC on EDWK (CPDLC above FL285 only!)
//
// This is the only source for it. The EuroScope SDK does not expose another
// controller's info block at all - CController offers callsign, position, frequency,
// rating and facility, and nothing textual.
//
// It answers a different question from the station table in SituCPDLC.txt. That table
// says which station a facility uses; this says whether the controller sitting there
// right now has datalink switched on, and at which station. Note LPPR_APP logs on as
// LPYP and EDWW_MAR_CTR as EDWK: the station is not derivable from the callsign, which
// is why the declaration is worth reading rather than guessing.
//
// Silence means do not offer. A controller who has not advertised CPDLC is not offered
// it on a handoff. See ShouldOfferCpdlc.
//
// The number behind that policy is worth recording, because the obvious one is
// misleading. Of the hundred and thirty three controllers online when this was written,
// nine declared CPDLC - about seven percent, which reads as far too thin a signal to
// gate on. But almost all of that population is towers, grounds, deliveries and ATIS
// stations, none of which has an en route datalink to declare. Counting only centres,
// which is the population this actually gates, eight of thirteen declared it.
// Advertising is the majority behaviour among the controllers it applies to.
//
// The cost of the policy is that a centre with CPDLC that does not advertise gets
// nothing offered. The cost of the opposite is uplinking to a station that is not
// listening, which fails at the far end where the sending controller cannot see it.
// The second is worse, and only the first is fixable by the affected controller, who
// need only add a line to their info block.
//
// Depends only on the standard library. See tests/CpdlcAtisTests.cpp.

#include <string>
#include <vector>

namespace SituCpdlcAtis
{
    struct Declaration
    {
        // The controller mentioned CPDLC, PDC or DCL somewhere in their info block.
        bool mentionsDatalink = false;

        // Specifically CPDLC, as opposed to PDC or DCL. The distinction matters: a
        // tower advertising departure clearance delivery is not an en route datalink
        // authority, and most of the declarations on the network are the former. For
        // next data authority, this is the flag to read.
        bool mentionsCpdlc = false;

        // The station code, when one could be picked out with confidence. Empty when
        // the controller advertised datalink without a code we could identify - which
        // is a normal outcome, not a failure. Fall back to the station table then.
        std::string station;
    };

    // Only centre and flight service positions hold en route datalink authority. A
    // tower, ground, delivery or ATIS station is never a next data authority however it
    // advertises itself, so its info block is not worth reading for this purpose - and
    // reading it is how a departure clearance code gets mistaken for a CPDLC station.
    inline bool IsEnRoutePosition(const std::string& callsign)
    {
        const size_t length = callsign.size();
        if (length >= 4 && callsign.compare(length - 4, 4, "_CTR") == 0) { return true; }
        if (length >= 4 && callsign.compare(length - 4, 4, "_FSS") == 0) { return true; }
        return false;
    }

    // The policy, in one named place rather than spread across call sites: offer CPDLC
    // only where the controller has said they have it. PDC and DCL deliberately do not
    // count - a tower delivering departure clearances is not an en route authority.
    inline bool ShouldOfferCpdlc(const Declaration& declaration)
    {
        return declaration.mentionsCpdlc;
    }

    inline char ToUpper(char c)
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }

    inline std::string Upper(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) { out.push_back(ToUpper(c)); }
        return out;
    }

    inline bool IsCodeChar(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    // A Hoppie login is four alphanumeric characters - TopSky's developer guide states
    // that, and every station in the shipped tables obeys it. Requiring exactly four
    // is what keeps ordinary prose from being read as a code: LOGON, ABOVE and FL300
    // are all rejected either by length or by being a known word.
    inline bool LooksLikeStation(const std::string& token)
    {
        if (token.size() != 4) { return false; }
        for (char c : token) { if (!IsCodeChar(c)) { return false; } }

        // Four identical characters is a placeholder, not a station - XXXX, ZZZZ, 0000
        // and friends are what people write when they mean "fill this in".
        bool allSame = true;
        for (size_t i = 1; i < token.size(); ++i)
        {
            if (token[i] != token[0]) { allSame = false; break; }
        }
        if (allSame) { return false; }

        // Words of exactly four characters that turn up next to a datalink mention and
        // are never a station, including the other common ways of writing a blank.
        static const char* const kNotStations[] = {
            "ONLY", "ABLE", "WITH", "FROM", "THIS", "PLEA", "VOIC", "DATA", "LINK",
            "USED", "FREE", "TEXT", "ATIS", "INFO", "NOTE", "SEE",
            "NONE", "NIL0", "TBAD", "ICAO", "CODE", "HERE", "SOON", "WHEN", "ASK0",
            "TEST", "DEMO", "XXX1", "ABCD", "1234"
        };
        for (const char* word : kNotStations)
        {
            if (token == word) { return false; }
        }
        return true;
    }

    // Every maximal run of alphanumerics in the line, uppercased.
    inline std::vector<std::string> Tokenise(const std::string& upperLine)
    {
        std::vector<std::string> tokens;
        std::string current;
        for (char c : upperLine)
        {
            if (IsCodeChar(c)) { current.push_back(c); }
            else if (!current.empty()) { tokens.push_back(current); current.clear(); }
        }
        if (!current.empty()) { tokens.push_back(current); }
        return tokens;
    }

    inline Declaration Parse(const std::vector<std::string>& atisLines)
    {
        Declaration result;

        for (const std::string& line : atisLines)
        {
            const std::string upper = Upper(line);

            const bool hasCpdlc = upper.find("CPDLC") != std::string::npos;
            const bool mentions = hasCpdlc
                               || upper.find("PDC") != std::string::npos
                               || upper.find("DCL") != std::string::npos;
            if (!mentions) { continue; }

            result.mentionsDatalink = true;
            if (hasCpdlc) { result.mentionsCpdlc = true; }

            // Only a line that names CPDLC can supply a CPDLC station. A code advertised
            // beside PDC or DCL alone is a departure clearance address, which is a
            // different service reached at a different code - "DCL [ZSPD]" and
            // "DCL [OMDA]" are both real, and neither is where an en route uplink goes.
            // A line saying "CPDLC/PDC on EDWK" does name CPDLC, so it still counts.
            if (!hasCpdlc) { continue; }
            if (!result.station.empty()) { continue; }

            // Attribute each code to the datalink keyword that owns it, rather than
            // taking the first one on the line.
            //
            // A line naming both services with different codes is real, and taking the
            // first code sends an en route uplink to a departure clearance address when
            // PDC happens to be written first - "PDC on EDDH, CPDLC on EDWW" yielded
            // EDDH. Position is not ownership.
            //
            // Adjacent keywords are one compound owner: "CPDLC/PDC on EDWK" is a single
            // station serving both, and the code after it belongs to CPDLC as much as
            // to PDC. Adjacency here means consecutive tokens, so the separator between
            // them - a slash, a dash, a space - does not matter.
            const std::vector<std::string> tokens = Tokenise(upper);

            bool ownerIsCpdlc = false;
            bool haveOwner = false;
            bool previousWasKeyword = false;

            std::string candidate;
            bool ambiguous = false;

            for (const std::string& token : tokens)
            {
                const bool isKeyword = (token == "CPDLC" || token == "PDC" || token == "DCL");

                if (isKeyword)
                {
                    if (previousWasKeyword)
                    {
                        // Compound: fold into the owner already open.
                        ownerIsCpdlc = ownerIsCpdlc || (token == "CPDLC");
                    }
                    else
                    {
                        ownerIsCpdlc = (token == "CPDLC");
                    }
                    haveOwner = true;
                    previousWasKeyword = true;
                    continue;
                }

                previousWasKeyword = false;

                // LOGON and ON introduce the code; they do not end the owner.
                if (token == "LOGON" || token == "ON") { continue; }

                if (!haveOwner || !ownerIsCpdlc) { continue; }
                if (!LooksLikeStation(token)) { continue; }

                // Repeats of one code are not ambiguity: a real line reads
                // "CPDLC LFPG | Charts chartfox.org/LFPG".
                if (candidate.empty()) { candidate = token; }
                else if (candidate != token) { ambiguous = true; }
            }

            if (!ambiguous) { result.station = candidate; }
        }

        return result;
    }
}
