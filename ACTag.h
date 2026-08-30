#pragma once
#include "EuroScopePlugIn.h"
#include "constants.h"
#include "CSiTRadar.h"
#include "SituPlugin.h"
#include "CFontHelper.h"
#include "TagCallsign.h"

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
    // The vertical movement indicator, drawn rather than typed.
    //
    // It used to be DrawText of "^" or "|", which the EuroScope tag font renders as half
    // triangles. Getting a real arrow out of that font would mean guessing which
    // character maps to one, and a wrong guess renders as whatever else is at that code
    // point with nothing to say it went wrong. Drawing the shape removes the guess.
    //
    // Occupies a fixed width slot and advances r.right past itself, so it drops into the
    // same left-to-right chain the DrawText calls around it use.
    static void DrawVMIArrow(CDC* dc, RECT& r, bool climbing);

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

