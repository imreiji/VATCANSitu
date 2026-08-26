#pragma once
#include "EuroScopePlugIn.h"
#include "constants.h"
#include "CSiTRadar.h"
#include "SituPlugin.h"
#include "CFontHelper.h"

using namespace std;

class CACTag
{
protected:

public:
    // Tags for FP predictions
    static void DrawFPACTag(CDC* hdc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, unordered_map<string, POINT>* tOffset);
    static void DrawFPConnector(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, COLORREF color, unordered_map<string, POINT>* tOffset);

    // Tags for Radar targets
    static void CACTag::DrawRTACTag(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, unordered_map<string, POINT>* tOffset);
    static void DrawNARDSTag(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, unordered_map<string, POINT>* tOffset);
    static void CACTag::DrawRTConnector(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, COLORREF color, unordered_map<string, POINT>* tOffset);
    static void DrawHistoryDots(CDC* dc, CRadarTarget* rt);
    static void DrawHistoryDots(CDC* dc, CFlightPlan* rt);

    // Short form of a CPDLC message for line 0 of the tag, e.g. "RC FL350" or "D/L".
    // Pure string work, no drawing.
    static std::string CPDLCMnemonicFor(const CPDLCMessage& message);

    // Drops the cached sector file airport positions used for destination distance,
    // forcing a rebuild on next use. The cache otherwise refreshes only when the sector
    // file name changes, so call this when a sector file may have been reloaded under
    // the same name.
    static void InvalidateAirportCache();
};

