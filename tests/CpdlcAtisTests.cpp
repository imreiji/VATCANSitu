// Tests for CpdlcAtis.h.
//
// The positive corpus is every CPDLC declaration that was live on the VATSIM network
// when this was written - nine controllers out of a hundred and thirty three. They are
// reproduced verbatim, including the trailing period, the parenthetical caveat and the
// altitude condition, because free text written by a hundred different controllers is
// the actual input and inventing tidier examples would test nothing.
//
// The negative corpus is ordinary controller info that must not be read as a station.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcAtisTests.cpp /Fe:CpdlcAtisTests.exe
//   CpdlcAtisTests.exe

#include "../CpdlcAtis.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void CheckDeclares(const std::vector<std::string>& lines,
                       const std::string& expectedStation,
                       const std::string& what)
    {
        ++g_checks;
        const SituCpdlcAtis::Declaration d = SituCpdlcAtis::Parse(lines);
        if (!d.mentionsDatalink || d.station != expectedStation)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected datalink with station \""
                      << expectedStation << "\", got "
                      << (d.mentionsDatalink ? "datalink" : "no datalink")
                      << " station \"" << d.station << "\"\n";
        }
    }

    void CheckSilent(const std::vector<std::string>& lines, const std::string& what)
    {
        ++g_checks;
        const SituCpdlcAtis::Declaration d = SituCpdlcAtis::Parse(lines);
        if (d.mentionsDatalink)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected no datalink mention, got station \""
                      << d.station << "\"\n";
        }
    }
}

int main()
{
    // --- The nine live declarations, verbatim.
    CheckDeclares({"\"Khartoum Center\" | CPDLC LOGON HSSS"}, "HSSS", "LOGON, bare code");
    CheckDeclares({"Porto Approach - CPDLC LOGON LPYP."}, "LPYP", "LOGON, trailing period");
    CheckDeclares({"Tehran Radar - CPDLC LOGON FL300+ [OIIX]"}, "OIIX", "bracketed wins over an altitude");
    CheckDeclares({"\"Jeddah Control\" - CPDLC/DCL [OEJD]"}, "OEJD", "bracketed, CPDLC/DCL");
    CheckDeclares({"Charts on https //chartfox.org | CPDLC Available on SBAZ | PDC and DLC are Available"},
                  "SBAZ", "prose, after on");
    CheckDeclares({"\"Bahrain Radar\" - CPDLC/DCL [OBBB]"}, "OBBB", "bracketed");
    CheckDeclares({"\"Muscat Control\" - CPDLC [OOM1]"}, "OOM1", "bracketed, digit in the code");
    CheckDeclares({"Milano Radar - CPDLC MWS2"}, "MWS2", "bare code straight after CPDLC");
    CheckDeclares({"Callsign BREMEN RADAR - CPDLC/PDC on EDWK (CPDLC above FL285 only!)"},
                  "EDWK", "prose with a parenthetical caveat");

    // --- The station is not the callsign. Both of these would be wrong if derived.
    CheckDeclares({"Porto Approach - CPDLC LOGON LPYP."}, "LPYP", "LPPR_APP logs on as LPYP");
    CheckDeclares({"Callsign BREMEN RADAR - CPDLC/PDC on EDWK (CPDLC above FL285 only!)"},
                  "EDWK", "EDWW_MAR_CTR logs on as EDWK");

    // --- Multi-line info blocks, which is the normal shape.
    CheckDeclares({"Welcome to Gander Oceanic",
                   "Charts at https://example.org",
                   "CPDLC LOGON CZQO",
                   "Please monitor 123.45"}, "CZQO", "declaration on the third line");

    // --- Case insensitivity, since controllers type however they like.
    CheckDeclares({"cpdlc logon czqm"}, "CZQM", "lower case");
    CheckDeclares({"Cpdlc/Dcl [czqx]"}, "CZQX", "mixed case, bracketed");

    // --- Datalink mentioned with no identifiable code. This is a normal outcome and
    //     must report the mention without inventing a station - the station table is
    //     the fallback.
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"CPDLC available, ask on frequency"});
        if (!d.mentionsDatalink || !d.station.empty())
        {
            ++g_failures;
            std::cout << "FAIL: mention without a code must yield no station, got \""
                      << d.station << "\"\n";
        }
    }
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"PDC available on request"});
        if (!d.mentionsDatalink || !d.station.empty())
        {
            ++g_failures;
            std::cout << "FAIL: PDC prose must not yield a station, got \"" << d.station << "\"\n";
        }
    }

    // --- Ambiguity is left alone rather than guessed. A line naming two codes has no
    //     single right answer and a wrong next data authority fails at the far end.
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"CPDLC CZQM or CZQX depending on sector"});
        if (!d.mentionsDatalink || !d.station.empty())
        {
            ++g_failures;
            std::cout << "FAIL: two candidates must yield no station, got \"" << d.station << "\"\n";
        }
    }

    // --- Words that are four characters and sit next to a datalink mention.
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"CPDLC ONLY above FL285"});
        if (d.station == "ONLY")
        {
            ++g_failures;
            std::cout << "FAIL: ONLY was read as a station\n";
        }
    }

    // --- The same code repeated on one line is not ambiguity. Taken verbatim from
    //     LFPG_N_GND. It yields no station because the line names PDC and not CPDLC,
    //     but the repeat must not be what stops it - so the same line with CPDLC on it
    //     is checked too.
    CheckDeclares({"TOBT @ cdm.vatsim.fr | PDC LFPG | Charts chartfox.org/LFPG"},
                  "", "a PDC only line yields no CPDLC station");
    CheckDeclares({"TOBT @ cdm.vatsim.fr | CPDLC LFPG | Charts chartfox.org/LFPG"},
                  "LFPG", "the same code twice on one line still resolves");

    // --- CPDLC is distinguished from PDC and DCL. A tower delivering clearances is
    //     not an en route datalink authority, and most declarations are the former.
    {
        ++g_checks;
        const auto tower = SituCpdlcAtis::Parse({"Pudong Delivery | DCL [ZSPD]"});
        const auto centre = SituCpdlcAtis::Parse({"Milano Radar - CPDLC MWS2"});
        if (tower.mentionsCpdlc || !tower.mentionsDatalink
            || !centre.mentionsCpdlc || !centre.mentionsDatalink)
        {
            ++g_failures;
            std::cout << "FAIL: DCL must not set mentionsCpdlc, CPDLC must\n";
        }
    }
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"\"Dubai Arrivals\" - DCL [OMDA] - Solo Endorsement"});
        if (d.mentionsCpdlc || !d.station.empty())
        {
            ++g_failures;
            std::cout << "FAIL: OMDB_APP DCL line - expected no station and no CPDLC flag, got \""
                      << d.station << "\"\n";
        }
    }

    // --- Controllers who say nothing about datalink. Under the offer policy these are
    //     treated as not having it, so what matters here is only that nothing is
    //     mistaken for a declaration.
    CheckSilent({}, "no info block at all");
    CheckSilent({""}, "one empty line");
    CheckSilent({"Toronto Centre", "Charts at https://example.org", "Monitor 132.8"},
                "ordinary info with no datalink mention");
    CheckSilent({"Runway 05 in use, expect ILS"}, "operational info");

    // --- A four character token elsewhere in an info block must not be picked up when
    //     datalink is never mentioned.
    CheckSilent({"Position CZQM staffed until 0200z"}, "a station-shaped token, no datalink word");

    // --- Only centre and flight service positions are read at all. A tower is never a
    //     next data authority however it advertises itself.
    {
        ++g_checks;
        const bool ok = SituCpdlcAtis::IsEnRoutePosition("CZQM_CTR")
                     && SituCpdlcAtis::IsEnRoutePosition("CZQM_2_CTR")
                     && SituCpdlcAtis::IsEnRoutePosition("CZQX_FSS")
                     && !SituCpdlcAtis::IsEnRoutePosition("EDDH_TWR")
                     && !SituCpdlcAtis::IsEnRoutePosition("ZSPD_DEL")
                     && !SituCpdlcAtis::IsEnRoutePosition("LFPG_N_GND")
                     && !SituCpdlcAtis::IsEnRoutePosition("OERK_ATIS")
                     && !SituCpdlcAtis::IsEnRoutePosition("OMDB_APP")
                     && !SituCpdlcAtis::IsEnRoutePosition("")
                     && !SituCpdlcAtis::IsEnRoutePosition("CTR");
        if (!ok) { ++g_failures; std::cout << "FAIL: en route position filter\n"; }
    }

    // --- A code advertised beside PDC or DCL alone is a departure clearance address,
    //     not an en route station. Both of these are real lines.
    CheckDeclares({"Pudong Delivery | DCL [ZSPD]"}, "", "DCL only, bracketed");
    CheckDeclares({"Callsign HAMBURG TOWER - PDC/DCL Logon EDDH"}, "", "PDC and DCL only, after LOGON");

    // --- But a line naming CPDLC alongside PDC still counts, because it names CPDLC.
    CheckDeclares({"Callsign BREMEN RADAR - CPDLC/PDC on EDWK (CPDLC above FL285 only!)"},
                  "EDWK", "CPDLC and PDC together");

    // --- A code belongs to the keyword that owns it, not to its position on the line.
    //     Both services with different codes on one line is real, and taking the first
    //     code sent an en route uplink to a departure clearance address whenever PDC
    //     was written first.
    CheckDeclares({"PDC on EDDH, CPDLC on EDWW"}, "EDWW", "PDC written first, prose");
    CheckDeclares({"PDC [EDDH] CPDLC [EDWW]"}, "EDWW", "PDC written first, bracketed");
    CheckDeclares({"PDC/DCL EDDH - CPDLC EDWW"}, "EDWW", "PDC compound written first");
    CheckDeclares({"CPDLC on EDWW, PDC on EDDH"}, "EDWW", "CPDLC written first, same answer");
    CheckDeclares({"CPDLC [EDWW] PDC [EDDH]"}, "EDWW", "CPDLC written first, bracketed");

    // --- Adjacent keywords are one owner: a single station serving both services.
    CheckDeclares({"CPDLC/PDC on EDWK"}, "EDWK", "CPDLC/PDC share one code");
    CheckDeclares({"PDC/CPDLC on EDWK"}, "EDWK", "the compound works in either order");
    CheckDeclares({"CPDLC/DCL [OEJD]"}, "OEJD", "CPDLC/DCL share one code");

    // --- Placeholders are not stations.
    {
        ++g_checks;
        const char* const placeholders[] = { "XXXX", "ZZZZ", "0000", "AAAA", "NONE", "ABCD", "1234", "TEST" };
        bool ok = true;
        for (const char* p : placeholders)
        {
            const auto d = SituCpdlcAtis::Parse({ std::string("CPDLC LOGON ") + p });
            if (!d.station.empty()) { ok = false; std::cout << "   accepted placeholder " << p << "\n"; }
        }
        if (!ok) { ++g_failures; std::cout << "FAIL: a placeholder was read as a station\n"; }
    }

    // --- The policy: silence means do not offer, and PDC or DCL alone is not enough.
    {
        ++g_checks;
        const bool centreDeclares = SituCpdlcAtis::ShouldOfferCpdlc(
            SituCpdlcAtis::Parse({"Milano Radar - CPDLC MWS2"}));
        const bool towerPdcOnly = SituCpdlcAtis::ShouldOfferCpdlc(
            SituCpdlcAtis::Parse({"Pudong Delivery | DCL [ZSPD]"}));
        const bool silentCentre = SituCpdlcAtis::ShouldOfferCpdlc(
            SituCpdlcAtis::Parse({"Toronto Centre", "Monitor 132.8"}));
        const bool noInfoAtAll = SituCpdlcAtis::ShouldOfferCpdlc(
            SituCpdlcAtis::Parse({}));

        if (!centreDeclares || towerPdcOnly || silentCentre || noInfoAtAll)
        {
            ++g_failures;
            std::cout << "FAIL: offer policy - declared=" << centreDeclares
                      << " dclOnly=" << towerPdcOnly
                      << " silent=" << silentCentre
                      << " noInfo=" << noInfoAtAll << "\n";
        }
    }

    // --- A controller who advertises CPDLC without a code is still offered it, with the
    //     station falling back to the table. Gating on a missing code would gate on the
    //     wrong thing.
    {
        ++g_checks;
        const auto d = SituCpdlcAtis::Parse({"CPDLC available, ask on frequency"});
        if (!SituCpdlcAtis::ShouldOfferCpdlc(d) || !d.station.empty())
        {
            ++g_failures;
            std::cout << "FAIL: declared without a code must still be offered\n";
        }
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
