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

        // Words of exactly four characters that turn up next to a datalink mention and
        // are never a station.
        static const char* const kNotStations[] = {
            "ONLY", "ABLE", "WITH", "FROM", "THIS", "PLEA", "VOIC", "DATA", "LINK",
            "USED", "FREE", "TEXT", "ATIS", "INFO", "NOTE", "SEE"
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

    // Text inside the first [...] on the line, uppercased. Controllers use brackets to
    // set the code apart from the surrounding prose, so it is the strongest signal
    // available and is tried first.
    inline std::string BracketedToken(const std::string& upperLine)
    {
        const size_t open = upperLine.find('[');
        if (open == std::string::npos) { return std::string(); }
        const size_t close = upperLine.find(']', open + 1);
        if (close == std::string::npos) { return std::string(); }
        return upperLine.substr(open + 1, close - open - 1);
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
            if (!result.station.empty()) { continue; }

            // 1. Bracketed, the clearest form.
            const std::string bracketed = BracketedToken(upper);
            if (LooksLikeStation(bracketed)) { result.station = bracketed; continue; }

            // 2. The token after LOGON or ON. Both are used as "the station is next".
            const std::vector<std::string> tokens = Tokenise(upper);
            for (size_t i = 0; i + 1 < tokens.size(); ++i)
            {
                if ((tokens[i] == "LOGON" || tokens[i] == "ON") && LooksLikeStation(tokens[i + 1]))
                {
                    result.station = tokens[i + 1];
                    break;
                }
            }
            if (!result.station.empty()) { continue; }

            // 3. A bare four character token after the datalink word, as in "CPDLC MWS2".
            //    Only taken when the line offers one distinct candidate, so a line
            //    naming several different codes is left for a human rather than
            //    guessed at. Repeats of the same code are not ambiguity: a real line
            //    reads "PDC LFPG | Charts chartfox.org/LFPG", where counting
            //    occurrences rather than distinct values would throw the answer away.
            std::string candidate;
            bool ambiguous = false;
            bool seenDatalinkWord = false;
            for (const std::string& token : tokens)
            {
                if (token == "CPDLC" || token == "PDC" || token == "DCL")
                {
                    seenDatalinkWord = true;
                    continue;
                }
                if (seenDatalinkWord && LooksLikeStation(token))
                {
                    if (candidate.empty()) { candidate = token; }
                    else if (candidate != token) { ambiguous = true; }
                }
            }
            if (!ambiguous) { result.station = candidate; }
        }

        return result;
    }
}
