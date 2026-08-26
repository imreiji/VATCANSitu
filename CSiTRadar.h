#pragma once
#include "EuroScopePlugIn.h"
#include "SituPlugin.h"
#include <chrono>
#include <string>
#include <map>
#include <set>
#include <unordered_map>
#include <array>
#include <regex>
#include <math.h>
#include <gdiplus.h>
#include <deque>
#include "constants.h"
#include "Scratchpad.h"
#include "cpdlc.h"
#include "wxRadar.h"
#include "CAsyncResponse.h"
#include <future>
#include <shared_mutex>
#include "CPopUpMenu.h"
#include "CAppWindows.h"
#include "TbsConfig.h"
#include "CpdlcStations.h"
#include "SettingsFile.h"
#include "SituFiles.h"
#include "SituLegacy.h"
#include "HaloTool.h"
#include "constants.h"
#include "TopMenu.h"
#include "ACTag.h"
#include "PPS.h"

using namespace EuroScopePlugIn;
using namespace std;

struct SSquawkCodeManagement {
    string squawk;
    string fpcs;
    int numCorrelatedRT{ 0 };
};


struct ACData {
    bool hasVFRFP;
    bool isADSB;
    bool isRVSM;
    bool isMedevac{ FALSE };
    int tagType{ 0 };
    bool isHandoff{ FALSE };
    bool isHandoffToMe{ FALSE };

    bool isJurisdictional{ FALSE };
    bool isOnScreen{ FALSE };
    bool isQuickLooked{ false };

    int tagWidth{ 65 }; 
    bool extAlt{ FALSE };
    int destLabelType{ 0 };
    deque<CPosition> prevPosition;
    bool pointOutToMe{ false };
    bool pointOutFromMe{ false };
    string POTarget{};
    clock_t POAcceptTime{ clock() };
    bool pointOutPendingApproval{ false };
    bool directToLineOn{ false };
    CPosition directToPendingPosition{};
    string directToPendingFixName;
    string POString{};
    ACRoute acFPRoute;
    bool multipleDiscrete{ false };

    // Set once the plugin has broken the automatic flight plan association for a
    // primary-only target, so it does not keep doing it and undo the controller's own
    // manual correlation. Reset when the target regains SSR. Replaces manualCorr, which
    // was read but never written, leaving its guard permanently false.
    bool autoCorrelationCleared{ false };
    int follower{ 1 }; // 0 is light, 1 is med, 2 heavy, 3 super

    // CPDLC. Messages exchanged with this aircraft, whether an unread downlink should
    // raise the mnemonic on the tag, and the logon state (0 not connected, 1 connected).
    vector<CPDLCMessage> CPDLCMessages;
    bool cpdlcMnemonic{ false };
    int cpdlcState{ 0 };
};

// Identifies the focused text field by ID rather than by address.
//
// This used to hold an STextField* pointing into a vector owned by a CAppWindows inside
// the radarScrWindows map. Closing a window destroyed that vector while the pointer was
// still live, and the keyboard hook kept writing through it until the next OnRefresh
// recomputed focus - up to a full second later, since EuroScope idles near 1 FPS.
// Resolve through CSiTRadar::GetFocusedTextField() instead, which validates both IDs.
struct SFocusItem {
    bool m_focus_on{ false };
    int m_window_id{ -1 };
    unsigned long m_text_field_id{ 0 };
};

struct SFreeText {
    int m_id{0};
    CPosition m_pos;
    string m_freetext_string;
};

struct buttonStates {
    bool haloTool;
    bool ptlTool;
    bool showExtrapFP{ FALSE };
    bool filterBypassAll{ FALSE };
    int ptlLength{ 3 };
    bool ptlAll{ FALSE };
    bool ebPTL{ false };
    bool wbPTL{ false };
    int haloRad;
    bool quickLook{ FALSE };
    bool quickLookDelta{ false };
    bool extAltToggle{ FALSE };
    int numJurisdictionAC{ 0 };
    bool setup{ false };
    bool destAirport{ false };
    bool crda{ false };
    bool haloCursor{ false };
    bool bigACID{ true };

    int numHistoryDots{ 4 };

    bool wxAll{ FALSE };
    bool wxHigh{ FALSE };
    bool wxOn{ FALSE };
    bool mvaDisp{ false };

    int numFreeText{ 0 };

    set<string> activeArpt;
    map<string, bool> nearbyCJS;
    vector<SFreeText> freetext;

    bool SFIMode{};
    SFocusItem focusedItem;
    bool handoffMode{};
    deque<string> jurisdictionalAC{}; // AC under jurisdiction + active handoffs to jurisdiction
    clock_t handoffModeStartTime{};
    size_t jurisdictionIndex{ 0 };

    bool destArptOn[5];
    string destICAO[5];
    bool destEST;
    bool destVFR;
    bool destDME;
    int tbsHdg;
    bool tbsMixed{ false };

    vector<CSectorElement> activeRunways;
    vector<CSectorElement> activeRunwaysList;
    vector<inactiveRunway> inactiveRwyList;

    // Rebuilt by updateActiveRunways. TBS takes its approach course from here rather
    // than from a typed heading and a hardcoded magnetic variation.
    vector<ActiveArrivalRunway> activeArrivalRunways;
    map<string, string> arptAltimeterOld;
    map<string, string> arptAtisLetterOld;

    bool CPDLCOn{ false };

    clock_t lastWxRefresh = 0;
    clock_t lastMetarRefresh = 0;
    clock_t lastAtisRefresh = 0;
    clock_t lastCPDLCPoll = 0;

    bool bgM3Click{ false };
    bool mouseMMB{ false };
    bool MB3menu{ false };
    POINT MB3clickedPt{ 0,0 };
    RECT MB3hoverRect{};
    RECT MB3primRect{};
    bool MB3SecondaryMenuOn{ true };
    bool MB3hoverOn{ false };
    int MB3menuType{ 0 }; // 0 for AC, 1 for freetext, more if needed
    int freetextselectedID{};
    string MB3SecondaryMenuType{};
    string SFIPrefString{};
    string SFIPrefStringASRSetting{};
    string SFIPrefStringDefault{ "ABCDEFGHIJ" };
    vector<string> ctrlRemarkDefaults{};

    void ResetSFIOptions() {
    if (SFIPrefStringASRSetting.size() == 0) {
        // default if not set via settings file
        SFIPrefString = SFIPrefStringDefault;
    }
    else {
        SFIPrefString = SFIPrefStringASRSetting;
    }
    };

    void ExpandSFIOptions() { SFIPrefString = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; };

    map<int, CAppWindows> radarScrWindows;
    vector<SSquawkCodeManagement> squawkCodes;

    // mAcData garbage cleaning
    vector<string> recentCallsignsSeen;
    bool acListMaint;
    clock_t lastAcListMaint{};


};

class CSiTRadar :
    public EuroScopePlugIn::CRadarScreen

{
public:

    CSiTRadar(void);
    virtual ~CSiTRadar(void);

    // Pointer back at screen
    static CRadarScreen* m_pRadScr;

    static unordered_map<string, ACData> mAcData;
    static unordered_map<string, int> tempTagData;
    static unordered_map<string, clock_t> hoAcceptedTime;
    // Published by the VATSIM datafeed worker, read on the main thread. Take
    // acCapabilityMutex for either.
    // Read once from SituTBS.txt at load. Static because the screens share it and
    // it never changes at run time.
    static SituTbs::Config tbsConfig;

    // CPDLC station table, read once from SituCPDLC.txt.
    static SituCpdlcStations::Table cpdlcStations;

    static unordered_map<string, bool> acADSB;
    static unordered_map<string, bool> acRVSM;
    static std::shared_mutex acCapabilityMutex;

    static buttonStates menuState;

    double wxRadLat;
    double wxRadLong;
    int wxRadZL;

    virtual void OnAsrContentLoaded(bool Loaded);
    void OnAsrContentToBeSaved();
    inline virtual void OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan);
    inline virtual void OnFlightPlanDisconnect(CFlightPlan FlightPlan);
    static void updateActiveRunways(int i);
    static void DisplayActiveRunways();
    inline virtual void OnControllerPositionUpdate(CController Controller);
    inline virtual void OnControllerDisconnect(CController Controller);
    // CPDLC downlink handoff.
    //
    // Upstream calls this work directly from OnRefresh, under the name asyncCPDLCFetch,
    // so a slow Hoppie stalls drawing for the length of the HTTP timeout. It can run on a
    // worker instead because cpdlc.cpp touches no EuroScope SDK - the only SDK reference
    // in that file is MakePDCMessage, which takes its CFlightPlan and CController as
    // parameters. So the thread polls and parses into the struct below, and the main
    // thread attaches the results to mAcData in DrainCPDLCPoll.
    struct SCPDLCPollResult {
        std::vector<CPDLCMessage> messages;
        std::string error;
        bool ready{ false };
    };

    static SCPDLCPollResult cpdlcPollResult;
    static std::mutex cpdlcPollMutex;
    static bool cpdlcPollInFlight;

    // Kicks off a poll if one is not already running. Returns immediately.
    static void StartCPDLCPoll();

    // Main thread only. Applies whatever the last poll produced.
    void DrainCPDLCPoll();

    static CAppWindows* GetAppWindow(int winID);

    // Resolves the "<windowId> <function>" screen-object id prefix to an open window.
    // Returns nullptr when the id is malformed or the window has already been closed.
    //
    // Callers previously did GetAppWindow(stoi(id))->... with neither guard: stoi throws
    // std::invalid_argument on a malformed id, and an exception escaping a EuroScope
    // callback terminates the process, while a closed window gave a null dereference.
    // Once this returns non-null, a later stoi(id) on the same id is safe by construction.
    static CAppWindows* GetAppWindowFromObjectId(const std::string& id) {
        if (id.empty()) { return nullptr; }
        try { return GetAppWindow(std::stoi(id)); }
        catch (...) { return nullptr; }
    }

    static void RegisterButton(RECT rect) {

    };

    void OnRefresh(HDC hdc, int phase);

    void CSiTRadar::OnButtonDownScreenObject(int ObjectType,
        const char* sObjectId,
        POINT Pt,
        RECT Area,
        int Button);

    inline virtual void OnClickScreenObject(int ObjectType,
        const char* sObjectId,
        POINT Pt,
        RECT Area,
        int Button);

    void CSiTRadar::OnMoveScreenObject(int ObjectType, 
        const char* sObjectId, 
        POINT Pt, 
        RECT Area, 
        bool Released);

    inline  virtual void    OnOverScreenObject(int ObjectType,
        const char* sObjectId,
        POINT Pt,
        RECT Area);

    inline  virtual void OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan,
        int DataType);


    void OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area);

    double RadRange(void)
    {
        RECT radarea = GetRadarArea();
        POINT pl = CPoint((int)radarea.left, (int)radarea.top);
        POINT pr = CPoint((int)radarea.right, (int)radarea.top);

        CPosition posL = ConvertCoordFromPixelToPosition(pl);
        CPosition posR = ConvertCoordFromPixelToPosition(pr);

        double raddist = posL.DistanceTo(posR);

        return raddist;
    }

    double PixelsPerNM(void)
    {
        RECT radarea = GetRadarArea();
        POINT pl = CPoint((int)radarea.left, (int)radarea.top);
        POINT pr = CPoint((int)radarea.right, (int)radarea.top);

        CPosition posL = ConvertCoordFromPixelToPosition(pl);
        CPosition posR = ConvertCoordFromPixelToPosition(pr);

        double raddist = posL.DistanceTo(posR);

        double rr = radarea.right;
        double rl = radarea.left;
        double radwidth = rr - rl;

        double pixpernm = radwidth / raddist;

        return pixpernm;
    };

    inline virtual void OnAsrContentToBeClosed(void) {

        // saving settings to the ASR file

        /*
        const char* sv = radtype.c_str();
        SaveDataToAsr("tagfamily", "Tag Family", sv);
        */

        delete this;
    };
    static bool halfSecTick; // toggles on and off every half second

    inline virtual void OnDoubleClickScreenObject(int ObjectType,
        const char* sObjectId,
        POINT Pt,
        RECT Area,
        int Button);

    // Sets, replaces or clears the SFI, leaving the controller remarks alone.
    //
    // The size()/at() chain this replaced had no branch for a two character scratchpad
    // whose first character was not a space, so newstring stayed empty and was written
    // back regardless - silently wiping the remarks. Clearing the SFI of a one character
    // remark dropped it for the same reason. See tests/ScratchpadTests.cpp, which runs
    // both implementations over a corpus and shows the only other differences are the
    // old code's trailing space.
    static bool ModifySFI(string c, CFlightPlan fp) {
        if (!strcmp(c.c_str(), "EXP")) {
            CSiTRadar::menuState.ExpandSFIOptions();
            return false;
        }

        const string scratchpad = fp.GetControllerAssignedData().GetScratchPadString();

        const string newstring = !strcmp(c.c_str(), "CLR")
            ? ScratchpadWithoutSfi(scratchpad)
            : ScratchpadWithSfi(scratchpad, c.empty() ? '\0' : c.at(0));

        fp.GetControllerAssignedData().SetScratchPadString(newstring.c_str());
        fp.GetFlightPlanData().AmendFlightPlan();
        return true;
    }

    // Resolves the focused text field, or nullptr when nothing is focused or the window
    // that owned it has since been closed. Clears the focus flag in that case so callers
    // do not keep retrying. Never cache the result: the next window close invalidates it.
    static STextField* GetFocusedTextField() {
        if (!menuState.focusedItem.m_focus_on) { return nullptr; }

        CAppWindows* window = GetAppWindow(menuState.focusedItem.m_window_id);
        if (window != nullptr) {
            for (auto& tf : window->m_textfields_) {
                if (tf.m_textFieldID == menuState.focusedItem.m_text_field_id) {
                    return &tf;
                }
            }
        }

        // Window or field is gone - the focus record is stale.
        menuState.focusedItem.m_focus_on = false;
        return nullptr;
    }

    static void SetFocusedTextField(int windowId, unsigned long textFieldId) {
        menuState.focusedItem.m_focus_on = true;
        menuState.focusedItem.m_window_id = windowId;
        menuState.focusedItem.m_text_field_id = textFieldId;
    }

    void ClearFocusedTextFields() {
        for (auto& win : menuState.radarScrWindows) {
            for (auto& tf : win.second.m_textfields_) {
                tf.m_focused = false;
            }
        }
        CSiTRadar::menuState.focusedItem.m_focus_on = false;
    }
    static void CloseWindow(int winID) {
        if (menuState.radarScrWindows.count(winID) != 0) {
            menuState.radarScrWindows.erase(winID);
        }
    }

    static void SendPointOut(const char* target, const char* message, CFlightPlan* fp) {

        fp->GetControllerAssignedData().SetFlightStripAnnotation(0, message);
        fp->PushFlightStrip(CSiTRadar::m_pRadScr->GetPlugIn()->ControllerSelectByPositionId(target).GetCallsign());

    }

    // Replaces the controller remarks, keeping any SFI. Had the same missing branch as
    // ModifySFI: a two character scratchpad not starting with a space produced an empty
    // string, so editing the remarks of such an aircraft blanked the field instead.
    static bool ModifyCtrlRemarks(string c, CFlightPlan fp) {
        const string scratchpad = fp.GetControllerAssignedData().GetScratchPadString();
        const string newstring = ScratchpadWithRemarks(scratchpad, c);

        fp.GetControllerAssignedData().SetScratchPadString(newstring.c_str());
        fp.GetFlightPlanData().AmendFlightPlan();
        return true;
    }
    inline  virtual void  OnFlightPlanFlightStripPushed(CFlightPlan FlightPlan,
        const char* sSenderController,
        const char* sTargetController);

protected:
    void ButtonToScreen(CSiTRadar* radscr, const RECT& rect, const string& btext, int itemtype);
    void DrawACList(POINT p, CDC* dc, unordered_map<string, ACData>& ac, int listType);

    // List positions are stored as offsets from the radar area origin - the same anchor
    // the header strip uses - so they travel with it rather than sitting at a fixed
    // screen coordinate. See the comment on ACList.
    static POINT ListOrigin(const ACList& list, const RECT& radarea);
    static void ClampListOffset(ACList& list, const RECT& radarea);
    static void ResolveListOffsets(const RECT& radarea);

    // Composes and sends one manual CPDLC uplink from the callsign menu.
    void SendCPDLCUplink(const std::string& which);

    // helper functions
    clock_t time = clock();
    clock_t oldTime = clock();

    // menu states
    bool halotool = FALSE;
    bool mousehalo = FALSE;
    int menuLayer = 0;
    bool altFilterOpts = FALSE;
    bool altFilterOn = TRUE;
    bool autoRefresh = FALSE;

    bool pressed = FALSE;
    int haloidx = 1; // default halo radius = 3, corresponds to index of the halooptions

    clock_t halfSec = 0;

    map<string, bool> hashalo;
    map<string, bool> hasPTL;
    map<string, bool> isBlinking;
    map<string, bool> isHandOffHold;
    map<string, string> ppsCJS;

    // ADSB Radar Site
    CPosition adsbSite; 

    // Tag Properties
    unordered_map<string, POINT> rtagOffset;
    unordered_map<string, POINT> fptagOffset;// the centre of the aircraft tag
    unordered_map<string, POINT> connectorOrigin; // the Tag end of the connector, this flips from right side to left side

    // menu functions
    RECT rLLim = { 0, 0, 10, 10 };
    RECT rHLim = { 0, 0, 10, 10 };

    // menu settings
    int altFilterLow = 0;
    int altFilterHigh = 0; 

    double halorad = 3;
    int haloOptions[6] = { 3,5,10,15,20,25 };
    int ptlOptions[20] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,20,25,30,35,40 };
    string controllerID;
    string radtype;
};
