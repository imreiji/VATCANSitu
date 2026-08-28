#pragma once
#include <EuroScopePlugIn.h>
#include <ctime>
#include <vector>
#include <string>

struct ACList {
    // Offset from the top left of the radar area, not an absolute screen position. The
    // header strip is anchored the same way - it is drawn from CPoint(radarea.left,
    // radarea.top) with everything inside it at a fixed offset - so a list keeps its
    // place relative to the header when the radar area moves or the window is resized.
    //
    // Stored absolutely before, which meant a list saved on a large monitor could sit
    // outside a smaller one entirely, with no way to drag it back.
    POINT offset{ 0, 0 };

    // Set when the position came from a settings file written before the change above.
    // The conversion needs the radar area, which is not known while the settings are
    // being read, so it happens on the first draw. See ResolveListOffsets.
    bool offsetIsAbsolute{ false };

    int listType{ 0 };
    bool collapsed{ false };
};

struct inactiveRunway {
    EuroScopePlugIn::CPosition end1;
    EuroScopePlugIn::CPosition end2;
};

// An arrival runway that the sector file currently has active, with the course an
// aircraft landing on it flies.
//
// trueCourse is computed from the two thresholds, so it needs no magnetic variation.
// magneticDesignator is the runway number times ten - 05 is 50 - which is what a typed
// course in the menu is matched against, magnetic on both sides.
struct ActiveArrivalRunway {
    std::string airport;
    std::string name;
    double trueCourse{ 0.0 };
    int magneticDesignator{ 0 };
};

struct ACRoute {
    std::vector<EuroScopePlugIn::CPosition> route_fix_positions;
    std::vector<std::string> fix_names;
    int nearestPtIdx;
    int directPtIdx;
    int selectIdx;
};

class SituPlugin :
    public EuroScopePlugIn::CPlugIn
{
public:

    SituPlugin();
    virtual ~SituPlugin();
    EuroScopePlugIn::CRadarScreen* OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated);

    virtual void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
        EuroScopePlugIn::CRadarTarget RadarTarget,
        int ItemCode,
        int TagData,
        char sItemString[16],
        int* pColorCode,
        COLORREF* pRGB,
        double* pFontSize);

    inline virtual void OnFunctionCall(int FunctionId,
        const char* sItemString,
        POINT Pt,
        RECT Area);

    virtual void OnAirportRunwayActivityChanged();

    inline virtual void OnCompilePrivateChat(const char* sSenderCallsign, const char* sReceiverCallsign, const char* sChatMessage);

    static void SendKeyboardPresses(std::vector<WORD> message);
    static void SendKeyboardString(std::string str);
    static POINT prevMousePt;
    static bool mouseAtRest;

    // When the halo last forced a redraw. The throttle above it counts mouse messages,
    // which puts no ceiling on the refresh rate at all - see the comment in MouseProc.
    static clock_t lastHaloRefresh;

    // Milliseconds between halo driven redraws. About 30 per second: fast enough that
    // the halo tracks the cursor smoothly, and an actual limit, which counting messages
    // is not.
    static const int kHaloRefreshIntervalMs = 33;

};