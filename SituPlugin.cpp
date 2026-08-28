#include "pch.h"
#include "SituPlugin.h"
#include "CSiTRadar.h"
#include "constants.h"
#include "ACTag.h"
#include "CFontHelper.h"

const int TAG_ITEM_IFR_REL = 5000;
const int TAG_FUNC_IFR_REL_REQ = 5001;
const int TAG_FUNC_IFR_RELEASED = 5002;

bool held = false;
bool injected = false;
bool kbF1 = false;
bool kbF3 = false;
bool kbF4 = false;
size_t jurisdictionIndex = 0;
size_t oldJurisdictionSize = 0;

POINT SituPlugin::prevMousePt = { 0,0 };
bool SituPlugin::mouseAtRest = false;
clock_t SituPlugin::lastHaloRefresh = 0;

HHOOK appHook;
HHOOK mouseHook;

// Takes a vector of keycodes and sends as keyboard commands
void SituPlugin::SendKeyboardPresses(vector<WORD> message)
{
    std::vector<INPUT> vec;
    for (auto ch : message)
    {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.dwFlags = KEYEVENTF_SCANCODE;
        input.ki.time = 0;
        input.ki.wVk = 0;
        input.ki.wScan = ch;
        input.ki.dwExtraInfo = 1;
        vec.push_back(input);

        input.ki.dwFlags |= KEYEVENTF_KEYUP;
        vec.push_back(input);
    }

    SendInput(vec.size(), &vec[0], sizeof(INPUT));
}

void SendMouseClick(DWORD mouseBut) {
    INPUT input{0};
    input.type = INPUT_MOUSE;
    input.mi.mouseData = 0;
    input.mi.time = 0;
    input.mi.dwFlags = mouseBut;

    SendInput(1, &input, sizeof(input));
}

void SituPlugin::SendKeyboardString(const string str) {
    std::vector<INPUT> vec;
    const auto key_board_layout = GetKeyboardLayout(0);

    INPUT iCaps = { 0 };
    iCaps.type = INPUT_KEYBOARD;
    iCaps.ki.dwFlags = 0;
    iCaps.ki.time = 0;
    iCaps.ki.wVk = VK_CAPITAL;
    iCaps.ki.wScan = MapVirtualKeyExW(VK_SHIFT, MAPVK_VK_TO_VSC, key_board_layout);
    iCaps.ki.dwExtraInfo = 0;
    vec.push_back(iCaps);

    iCaps.ki.dwFlags |= KEYEVENTF_KEYUP;
    vec.push_back(iCaps);

    for (auto ch : str)
    {
        INPUT input = { 0 };
        if (ch == '_') {
            INPUT inputShift = { 0 };
            inputShift.type = INPUT_KEYBOARD;
            inputShift.ki.dwFlags = 0;
            inputShift.ki.time = 0;
            inputShift.ki.wVk = VK_SHIFT;
            inputShift.ki.wScan = MapVirtualKeyExW(VK_SHIFT, MAPVK_VK_TO_VSC, key_board_layout);
            inputShift.ki.dwExtraInfo = 0;
            vec.push_back(inputShift);

            input.type = INPUT_KEYBOARD;
            input.ki.dwFlags = 0;
            input.ki.time = 0;
            input.ki.wVk = VK_OEM_MINUS;
            input.ki.wScan = MapVirtualKeyExW(VK_OEM_MINUS, MAPVK_VK_TO_VSC, key_board_layout);
            input.ki.dwExtraInfo = 0;
            vec.push_back(input);

            input.ki.dwFlags |= KEYEVENTF_KEYUP;
            vec.push_back(input);

            inputShift.ki.dwFlags |= KEYEVENTF_KEYUP;
            vec.push_back(inputShift);
        }
        else {

            input.type = INPUT_KEYBOARD;
            input.ki.dwFlags = 0;
            input.ki.time = 0;
            input.ki.wVk = VkKeyScanExW(ch, key_board_layout);
            input.ki.wScan = MapVirtualKeyExW(VkKeyScanExW(ch, key_board_layout), MAPVK_VK_TO_VSC, key_board_layout);
            input.ki.dwExtraInfo = 0;
            vec.push_back(input);

            input.ki.dwFlags |= KEYEVENTF_KEYUP;
            vec.push_back(input);
        }
    }

    iCaps.ki.dwFlags = 0;
    vec.push_back(iCaps);

    iCaps.ki.dwFlags |= KEYEVENTF_KEYUP;
    vec.push_back(iCaps);

    SendInput(vec.size(), vec.data(), sizeof(INPUT));
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {

    if (CSiTRadar::m_pRadScr == nullptr) { return CallNextHookEx(NULL, nCode, wParam, lParam); }

    // Resolve the focused field through its window and field IDs rather than following a
    // stored pointer. Closing a window destroyed the vector the old STextField* pointed
    // into, and every keystroke below then wrote to freed memory until the next
    // OnRefresh recomputed focus. GetFocusedTextField returns nullptr for a stale record
    // and clears the flag; GetAppWindow can likewise return nullptr and was never checked.
    CAppWindows* parentWin = CSiTRadar::GetAppWindow(CSiTRadar::menuState.focusedItem.m_window_id);
    STextField* focusedField = CSiTRadar::GetFocusedTextField();

    if (parentWin != nullptr && focusedField != nullptr) {
        if (!(lParam & 0x40000000)) {
            if (wParam >= 0x30 && wParam <= 0x5A) {
                char l = MapVirtualKeyA(wParam, 2);

                focusedField->m_text.push_back(l);
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_OEM_PERIOD) {
                focusedField->m_text.push_back('.');
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_OEM_PLUS) {
                focusedField->m_text.push_back('+');
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_OEM_MINUS) {
                focusedField->m_text.push_back('-');
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_OEM_2) {
                focusedField->m_text.push_back('/');
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_SPACE) {
                focusedField->m_text.push_back(' ');
                CSiTRadar::m_pRadScr->RequestRefresh();
                return -1;
            }
            if (wParam == VK_BACK) {
                if (!focusedField->m_text.empty()) {
                    focusedField->m_text.pop_back();
                    CSiTRadar::m_pRadScr->RequestRefresh();
                }
                return -1;
            }
            if (wParam == VK_RETURN) {
                if (parentWin->m_winType == WINDOW_HANDOFF_EXT_CJS) {

                    CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(parentWin->m_callsign.c_str()).InitiateHandoff(
                        CSiTRadar::m_pRadScr->GetPlugIn()->ControllerSelectByPositionId(focusedField->m_text.c_str()).GetCallsign()
                    );
                    // Closing the window destroys parentWin and focusedField - nothing
                    // below may touch them.
                    CSiTRadar::CloseWindow(parentWin->m_windowId_);
                    CSiTRadar::menuState.focusedItem.m_focus_on = false;
                    CSiTRadar::m_pRadScr->RequestRefresh();
                    return -1;
                }
                if (parentWin->m_winType == WINDOW_CTRL_REMARKS) {

                    CSiTRadar::ModifyCtrlRemarks(focusedField->m_text.c_str(), CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(parentWin->m_callsign.c_str()));
                    CSiTRadar::CloseWindow(parentWin->m_windowId_);
                    CSiTRadar::menuState.focusedItem.m_focus_on = false;
                    CSiTRadar::m_pRadScr->RequestRefresh();
                    return -1;
                }
            }
        }
    }

    if (CSiTRadar::menuState.SFIMode) {
        if (wParam > 0x40 && wParam < 0x5A) {
            char l = MapVirtualKeyA(wParam,2);
            string sfi;
            sfi = l;

            CSiTRadar::ModifySFI(sfi, CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelectASEL());
            CSiTRadar::menuState.SFIMode = false;
            return -1;
        }

    }

    if (
        wParam == VK_F1 ||
        wParam == VK_F3 ||
        wParam == VK_F4 ||
        wParam == VK_F9 ||
        wParam == VK_RETURN ||
        wParam == VK_ESCAPE ||
        wParam == VK_SNAPSHOT
        ) {

        if (!(lParam & 0x40000000)) { // if bit 30 is 0 this will evaluate true means key was previously up

            switch (wParam) {
            case VK_RETURN:
            {
                if (CSiTRadar::menuState.handoffMode) {
                    SituPlugin::SendKeyboardPresses({ 0x4E, 0x01 });
                    CSiTRadar::menuState.handoffMode = FALSE;
                }
                CSiTRadar::m_pRadScr->RequestRefresh();
                return 0;
            }

            case VK_F1: {

                if (GetAsyncKeyState(VK_F1) & 0x8000) {
                    kbF1 = true;
                    if (CSiTRadar::m_pRadScr->GetPlugIn()->RadarTargetSelectASEL().IsValid()) {
                        SituPlugin::SendKeyboardPresses({ 0x01 });
                    }
                    return -1;
                }
                // If the key is no longer physically down, fall out of the switch to the
                // shared "return -1" below and let the key-up branch handle it, exactly as
                // VK_F9 / VK_SNAPSHOT already do. Without this break, control ran on into
                // the F3, F4 and ESCAPE cases and cancelled handoff mode.
                break;
            }

            case VK_F3: {
                if (GetAsyncKeyState(VK_F3) & 0x8000) {
                    kbF3 = true;
                    return -1;
                }
                break;
            }

            case VK_F4: {
                if (GetAsyncKeyState(VK_F4) & 0x8000) {
                    kbF4 = true;
                    if (CSiTRadar::m_pRadScr->GetPlugIn()->RadarTargetSelectASEL().IsValid()) {
                        SituPlugin::SendKeyboardPresses({ 0x01 });
                    }
                    return -1;
                }
                break;
            }

            case VK_ESCAPE: {
                
                if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                    if (CSiTRadar::menuState.handoffMode == TRUE  
                        || CSiTRadar::menuState.SFIMode == TRUE) {
                        
                        CSiTRadar::menuState.SFIMode = false;
                        CSiTRadar::menuState.handoffMode = FALSE;
                        CSiTRadar::menuState.jurisdictionIndex = 0;
                        CSiTRadar::m_pRadScr->RequestRefresh();

                        return 0;
                    }
                    else { return 0; }
                }
                
                else {
                    CSiTRadar::m_pRadScr->RequestRefresh();
                    return 0;
                }
            }

            }


            return -1;
        }
        else { // if key was previously down
            if (!(lParam & 0x80000000)) { // if bit 31 is 0 this will evaluate true, which means key is being pressed
                if (!(lParam & 0x0000ffff)) {  // if no repeats
                    return -1;
                }
                else {
                    held = true;
                    // Long Press Keyboard Commands will send the function direct to ES



                    // *** END LONG PRESS COMMANDS ***
                    return 0;
                }
            }
            else {
                if (held == false) {
                    // START OF SHORT PRESS KEYBOARD COMMANDS ***
                    switch (wParam) {
                    case VK_F1: {
                        // Toggle on hand-off mode
                        if (kbF1) {
                            if (CSiTRadar::menuState.handoffMode == FALSE ||
                                CSiTRadar::menuState.jurisdictionalAC.size() != oldJurisdictionSize) {
                                oldJurisdictionSize = CSiTRadar::menuState.jurisdictionalAC.size();
                                CSiTRadar::menuState.jurisdictionIndex = 0;
                            }

                            CSiTRadar::menuState.handoffMode = TRUE;
                            CSiTRadar::menuState.SFIMode = FALSE;
                            CSiTRadar::menuState.handoffModeStartTime = clock();

                            // ASEL the next aircraft in the handoff priority list
                            if (!CSiTRadar::menuState.jurisdictionalAC.empty()) {
                                if (CSiTRadar::menuState.jurisdictionIndex < CSiTRadar::menuState.jurisdictionalAC.size()) {
                                    CSiTRadar::m_pRadScr->GetPlugIn()->SetASELAircraft(CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(CSiTRadar::menuState.jurisdictionalAC.at(CSiTRadar::menuState.jurisdictionIndex).c_str()));
                                    // if plane is being handed off to me, use F3 to accept handoff instead of F4 to deny
                                    if (
                                        strcmp(
                                            CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(CSiTRadar::menuState.jurisdictionalAC.at(CSiTRadar::menuState.jurisdictionIndex).c_str()).GetHandoffTargetControllerId(),
                                            CSiTRadar::m_pRadScr->GetPlugIn()->ControllerMyself().GetPositionId()) == 0
                                        )
                                    {

                                        SituPlugin::SendKeyboardPresses({ 0x3D }); // send F3
                                    }
                                    else {

                                        SituPlugin::SendKeyboardPresses({ 0x3E }); // send F4 in keyboard presses
                                    }
                                    CSiTRadar::menuState.jurisdictionIndex++;
                                }
                                else {
                                    CSiTRadar::menuState.jurisdictionIndex = 0;
                                    CSiTRadar::menuState.handoffMode = FALSE;
                                    SituPlugin::SendKeyboardPresses({ 0x01 });
                                }
                            }

                            CSiTRadar::m_pRadScr->RequestRefresh();

                            held = false;
                            kbF1 = false;
                            return -1;
                        }
                        return 0;
                    }

                    case VK_F3:
                    {
                        
                        if (kbF3) {

                            CSiTRadar::menuState.ptlAll = !CSiTRadar::menuState.ptlAll;
                            CSiTRadar::m_pRadScr->RequestRefresh();

                            kbF3 = false;
                            held = false;
                            return -1;
                        }
                        else {
                            

                        }
                        return 0;
                    }

                    case VK_F4:
                    {
                        if (kbF4) {
                            if (CSiTRadar::menuState.SFIMode == FALSE ||
                                CSiTRadar::menuState.jurisdictionalAC.size() != oldJurisdictionSize) {
                                oldJurisdictionSize = CSiTRadar::menuState.jurisdictionalAC.size();
                                CSiTRadar::menuState.jurisdictionIndex = 0;
                            }

                            CSiTRadar::menuState.SFIMode = TRUE;
                            CSiTRadar::menuState.handoffMode = FALSE;
                            CSiTRadar::menuState.handoffModeStartTime = clock();

                            // ASEL the next aircraft in the handoff priority list
                            if (!CSiTRadar::menuState.jurisdictionalAC.empty()) {
                                if (CSiTRadar::menuState.jurisdictionIndex < CSiTRadar::menuState.jurisdictionalAC.size()) {

                                    // Cycle through jurisdictional aircraft and ASEL them
                                    CSiTRadar::m_pRadScr->GetPlugIn()->SetASELAircraft(CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(CSiTRadar::menuState.jurisdictionalAC.at(CSiTRadar::menuState.jurisdictionIndex).c_str()));
                                    CSiTRadar::menuState.jurisdictionIndex++;
                                }
                                else {
                                    CSiTRadar::menuState.jurisdictionIndex = 0;
                                    CSiTRadar::menuState.SFIMode = FALSE;
                                    //SituPlugin::SendKeyboardPresses({ 0x01 });
                                }
                            }

                            CSiTRadar::m_pRadScr->RequestRefresh();

                            held = false;
                            kbF4 = false;
                            return -1;
                        }
                        return 0;
                    }

                    case VK_F9:
                    {

                        if (CSiTRadar::menuState.filterBypassAll == FALSE) {
                            CSiTRadar::menuState.filterBypassAll = TRUE;

                            for (auto& p : CSiTRadar::mAcData) {
                                CSiTRadar::tempTagData[p.first] = p.second.tagType;
                                // Do not open uncorrelated tags
                                if (p.second.tagType == 0) {
                                    p.second.tagType = 1;
                                }
                            }

                        }
                        else if (CSiTRadar::menuState.filterBypassAll == TRUE) {

                            for (auto& p : CSiTRadar::tempTagData) {
                                // prevents closing of tags that became under your jurisdiction during quicklook
                                if (!CSiTRadar::m_pRadScr->GetPlugIn()->FlightPlanSelect(p.first.c_str()).GetTrackingControllerIsMe()) {
                                    CSiTRadar::mAcData[p.first].tagType = p.second;
                                }
                            }

                            CSiTRadar::tempTagData.clear();
                            CSiTRadar::menuState.filterBypassAll = FALSE;
                        }

                        CSiTRadar::m_pRadScr->RequestRefresh();

                        held = false;
                        return -1;
                    }

                    case VK_SNAPSHOT: {

                        CSiTRadar::menuState.mvaDisp = !CSiTRadar::menuState.mvaDisp;

                        CSiTRadar::m_pRadScr->GetPlugIn()->SelectActiveSectorfile();
                        for (CSectorElement sectorElement = CSiTRadar::m_pRadScr->GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_FREE_TEXT); sectorElement.IsValid();
                            sectorElement = CSiTRadar::m_pRadScr->GetPlugIn()->SectorFileElementSelectNext(sectorElement, SECTOR_ELEMENT_FREE_TEXT)) {

                            string name = sectorElement.GetName();
                            if(name.find("VFR Call-Up") != string::npos) {

                                    CSiTRadar::m_pRadScr->ShowSectorFileElement(sectorElement, sectorElement.GetComponentName(0), CSiTRadar::menuState.mvaDisp);

                            }
                        }

                        CSiTRadar::m_pRadScr->RefreshMapContent();

                        return -1;

                    }

                    // *** END OF SHORT KEYBOARD PRESS COMMANDS ***
                    }          
                }
                held = false;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {

    // Win32 contract: when nCode < 0 the hook must pass the call straight on without
    // processing it - wParam/lParam are not guaranteed valid. lParam was previously
    // dereferenced above this check.
    if (nCode < 0) {
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    POINT Pt;
    MOUSEHOOKSTRUCT* mouseStruct = (MOUSEHOOKSTRUCT*)lParam;
    RECT windowRect;
    GetWindowRect(GetActiveWindow(), &windowRect);
    Pt.x = mouseStruct->pt.x - windowRect.left;
    Pt.y = mouseStruct->pt.y - windowRect.top;

    int deltaPx, deltaPy;
    deltaPx = abs(Pt.x - SituPlugin::prevMousePt.x);
    deltaPy = abs(Pt.y - SituPlugin::prevMousePt.y);

    SituPlugin::prevMousePt = Pt;
    
    RECT winRect{};
    GetWindowRect(GetActiveWindow(), &winRect);

    if (nCode == HC_ACTION) {

        if (wParam == WM_MOUSEMOVE) {
            if (CSiTRadar::menuState.MB3hoverOn) {

                if (Pt.x < CPopUpMenu::totalRect.left ||
                    Pt.x > CPopUpMenu::totalRect.right ||
                    Pt.y < CPopUpMenu::totalRect.top ||
                    Pt.y > CPopUpMenu::totalRect.bottom) {
                    CSiTRadar::menuState.MB3hoverOn = false;
                    if (CSiTRadar::m_pRadScr != nullptr) {
                        CSiTRadar::m_pRadScr->RequestRefresh();
                    }
                }
            }
            if (CSiTRadar::menuState.haloCursor) {

                // Redraws are limited by elapsed time, not by a count of mouse messages.
                //
                // The old throttle was "every fifth WM_MOUSEMOVE", which sounds like a
                // limit and is not one: nothing bounds how fast those five arrive. Each
                // full scope repaint finishes, the pump dispatches the moves that queued
                // behind it, the fifth asks for another repaint, and around it goes. The
                // loop runs exactly as fast as the machine can redraw the entire screen -
                // every target, every tag, the weather raster - which is why the README
                // says CPU use rises dramatically with the halo on, and why it can starve
                // the message pump enough to stop other EuroScope windows opening.
                //
                // A ceiling in milliseconds is the thing that was missing. Thirty a
                // second looks identical on a cursor and is a real bound.
                if (deltaPx == 0 && deltaPy == 0)
                {
                    SituPlugin::mouseAtRest = true;
                }
                else {
                    SituPlugin::mouseAtRest = false;
                }

                const clock_t now = clock();
                const bool intervalElapsed =
                    ((now - SituPlugin::lastHaloRefresh) * 1000 / CLOCKS_PER_SEC)
                        >= SituPlugin::kHaloRefreshIntervalMs;

                // Moving off from stationary still redraws at once, so the halo does not
                // lag behind the first movement by up to a frame.
                if (SituPlugin::mouseAtRest || intervalElapsed) {
                    if (CSiTRadar::m_pRadScr != nullptr) {
                        CSiTRadar::m_pRadScr->RequestRefresh();
                    }
                    SituPlugin::lastHaloRefresh = now;
                }
            }
        }

        if (CSiTRadar::menuState.handoffMode 
            || (CSiTRadar::menuState.MB3menu && !CSiTRadar::menuState.MB3hoverOn) 
            || CSiTRadar::menuState.SFIMode
            || (CSiTRadar::menuState.bgM3Click && !CSiTRadar::menuState.MB3hoverOn)) {

            if (wParam == WM_LBUTTONDOWN || wParam == WM_MBUTTONDOWN || wParam == WM_RBUTTONDOWN) {

                CSiTRadar::menuState.handoffMode = false;
                CSiTRadar::menuState.SFIMode = false;
                CSiTRadar::menuState.MB3menu = false;
                CSiTRadar::menuState.bgM3Click = false;
                CSiTRadar::m_pRadScr->RequestRefresh();

                if (!CSiTRadar::menuState.jurisdictionalAC.empty()) {
                    SituPlugin::SendKeyboardPresses({ 0x01 });
                }
                return -1;
            }
            return 0;

        } // untoggle h/o if a click happens

        switch (wParam) {
        // A middle double-click delivers DOWN, UP, DBLCLK, UP - the DBLCLK stands in for the
        // second press, so it has to behave like WM_MBUTTONDOWN. It used to fall through into
        // WM_RBUTTONDOWN, which left the WM_MBUTTONUP that follows emitting an unmatched
        // synthetic left-button release (and clobbered the right-click menu anchor point).
        case WM_MBUTTONDBLCLK:
        case WM_MBUTTONDOWN: {
            CSiTRadar::menuState.mouseMMB = true;
            SendMouseClick(MOUSEEVENTF_LEFTDOWN);
            CallNextHookEx(NULL, nCode, wParam, lParam);
            return -1;
        }
        case WM_MBUTTONUP: {
            CSiTRadar::menuState.mouseMMB = false;
            SendMouseClick(MOUSEEVENTF_LEFTUP);
            CallNextHookEx(NULL, nCode, wParam, lParam);
            return -1;
        }
        case WM_RBUTTONDOWN: {
            CSiTRadar::menuState.MB3clickedPt = Pt;
            CSiTRadar::menuState.MB3hoverRect = { 0,0,0,0 };
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        case WM_RBUTTONUP: {
            if (Pt.x == CSiTRadar::menuState.MB3clickedPt.x &&
                Pt.y == CSiTRadar::menuState.MB3clickedPt.y) {

                CSiTRadar::menuState.bgM3Click = true;

            }
            CSiTRadar::menuState.MB3clickedPt = Pt;
            CSiTRadar::menuState.MB3hoverRect = { 0,0,0,0 };
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        default: {
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }
        }

    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

SituPlugin::SituPlugin()
	: EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
		"VATCANSitu",
		"0.5.11.0",
		"Ron Yan",
		"Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)")
{
    RegisterTagItemType("IFR Release", TAG_ITEM_IFR_REL);
    RegisterTagItemType("CPDLC State", TAG_ITEM_CPDLC);
    RegisterTagItemFunction("Request IFR Release", TAG_FUNC_IFR_REL_REQ);
    RegisterTagItemFunction("Grant IFR Release", TAG_FUNC_IFR_RELEASED);
    RegisterTagItemFunction("Open CPDLC Menu", TAG_FUNCTION_OPEN_CPDLC_WINDOW);

    DWORD appProc = GetCurrentThreadId();
    appHook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, NULL, appProc);
    mouseHook = SetWindowsHookEx(WH_MOUSE, MouseProc, NULL, appProc);

    CFontHelper::CreateFonts();

}

SituPlugin::~SituPlugin()
{

    CFontHelper::DeleteFonts();

    UnhookWindowsHookEx(appHook);
    UnhookWindowsHookEx(mouseHook);
}

EuroScopePlugIn::CRadarScreen* SituPlugin::OnRadarScreenCreated(const char* sDisplayName, bool NeedRadarContent, bool GeoReferenced, bool CanBeSaved, bool CanBeCreated)
{
    return new CSiTRadar;
}

void SituPlugin::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
    EuroScopePlugIn::CRadarTarget RadarTarget,
    int ItemCode,
    int TagData,
    char sItemString[16],
    int* pColorCode,
    COLORREF* pRGB,
    double* pFontSize) {

    if (ItemCode == TAG_ITEM_IFR_REL) {

        // Read the release state through the shared parser. The strncmp(..., "RREQ", 4)
        // this replaced also matched any remark beginning with those four letters, so a
        // controller remark of "RREQUEST FUEL" rendered as an outstanding release request.
        const Scratchpad fields = ParseScratchpad(
            FlightPlan.GetControllerAssignedData().GetScratchPadString());

        *pColorCode = TAG_COLOR_RGB_DEFINED;

        switch (fields.release) {
        case ReleaseState::Requested:
            strcpy_s(sItemString, 16, "\u00A4");
            *pRGB = C_PPS_ORANGE;
            break;

        case ReleaseState::Granted:
            strcpy_s(sItemString, 16, "\u00A4");
            *pRGB = RGB(9, 171, 0);
            break;

        default:
            strcpy_s(sItemString, 16, "\u00AC");
            *pRGB = C_PPS_ORANGE;
            break;
        }
    }

    if (ItemCode == TAG_ITEM_CPDLC) {

        // Marks aircraft that have exchanged CPDLC at all, so the tag function is
        // discoverable on the ones it applies to. find rather than at or operator[]:
        // this is polled for every drawn tag, and operator[] would insert an entry for
        // every aircraft on the scope into a map that is garbage collected on the
        // assumption it only holds ones we have actually seen.
        const auto entry = CSiTRadar::mAcData.find(FlightPlan.GetCallsign());
        if (entry != CSiTRadar::mAcData.end() && !entry->second.CPDLCMessages.empty()) {
            *pColorCode = TAG_COLOR_RGB_DEFINED;
            strcpy_s(sItemString, 16, "\u00A4");
            *pRGB = C_CPDLC_GREEN;
        }
    }

}

inline void SituPlugin::OnFunctionCall(int FunctionId, const char* sItemString, POINT Pt, RECT Area)
{
    CFlightPlan fp;
    fp = FlightPlanSelectASEL();
    string spString = fp.GetControllerAssignedData().GetScratchPadString();

    if (FunctionId == TAG_FUNCTION_OPEN_CPDLC_WINDOW) {

        // Nothing to open a window against, and GetCallsign on an invalid plan has
        // nothing meaningful to return.
        if (!fp.IsValid()) { return; }

        const string callsign = fp.GetCallsign();

        // Bring an already open window for this aircraft to the click instead of
        // stacking a second one on top of it.
        for (auto& win : CSiTRadar::menuState.radarScrWindows) {
            if (win.second.m_winType == WINDOW_CPDLC && win.second.m_callsign == callsign) {
                win.second.m_origin = { Pt.x, Pt.y };
                if (CSiTRadar::m_pRadScr != nullptr) { CSiTRadar::m_pRadScr->RequestRefresh(); }
                return;
            }
        }

        // No radar screen means no radar area to place the window inside.
        if (CSiTRadar::m_pRadScr == nullptr) { return; }

        // operator[] rather than at: an aircraft that has never been drawn is not in
        // mAcData yet, and at would throw out of a EuroScope callback. The window is
        // opened against an empty message list in that case, which is correct - there
        // is no CPDLC history to show.
        CAppWindows cpdlc({ Pt.x, Pt.y }, WINDOW_CPDLC, fp,
            CSiTRadar::m_pRadScr->GetRadarArea(),
            CSiTRadar::mAcData[callsign].CPDLCMessages);
        cpdlc.m_callsign = callsign;
        CSiTRadar::menuState.radarScrWindows[cpdlc.m_windowId_] = cpdlc;

        CSiTRadar::m_pRadScr->RequestRefresh();
        return;
    }

    // Toggle: an outstanding request or an existing grant both clear.
    //
    // This used to write the field twice - once with "" and then again with the tail -
    // so an empty string was briefly published to the network, and any SFI in the tail
    // was left where the SFI parser could no longer see it. Going through the shared
    // parser writes once and keeps the SFI and remarks intact either way.
    if (FunctionId == TAG_FUNC_IFR_REL_REQ) {
        const ReleaseState current = ParseScratchpad(spString).release;
        const ReleaseState next = (current == ReleaseState::None)
            ? ReleaseState::Requested
            : ReleaseState::None;

        fp.GetControllerAssignedData().SetScratchPadString(
            ScratchpadWithRelease(spString, next).c_str());
    }

    if (FunctionId == TAG_FUNC_IFR_RELEASED) {

        // Only allow if APP, DEP or CTR
        if (ControllerMyself().GetFacility() >= 5) {

            if (ParseScratchpad(spString).release == ReleaseState::Requested) {
                fp.GetControllerAssignedData().SetScratchPadString(
                    ScratchpadWithRelease(spString, ReleaseState::Granted).c_str());
            }
        }
    }
}

void SituPlugin::OnAirportRunwayActivityChanged()
{
    // DisplayActiveRunways() dereferences m_pRadScr too, so it belongs inside the guard.
    // ~CSiTRadar sets m_pRadScr back to nullptr, so this is reachable once the last ASR closes.
    if (CSiTRadar::m_pRadScr != nullptr) {
        CSiTRadar::updateActiveRunways(0);
        CSiTRadar::DisplayActiveRunways();
    }
}

void SituPlugin::OnCompilePrivateChat(const char* sSenderCallsign,
    const char* sReceiverCallsign,
    const char* sChatMessage)
{

    string s, cs, msg;
    s = sChatMessage;
    string::size_type pos = s.find(" ");
    if (pos != s.npos) {
        cs = s.substr(0, pos);
        msg = s.substr(pos + 1);
    }

    for (auto& c : cs) {
        c = toupper(c);
    }

    for (auto& c : msg) {
        c = toupper(c);
    }

    // find(), not operator[]. This ran on every private message received, and cs is just
    // the first word of whatever was typed - so operator[] default-constructed an ACData
    // keyed on that word. A message of "hello there" created an aircraft called HELLO,
    // which then showed up in the off-screen list until the five minute garbage collector
    // in OnRefresh swept it.
    if (cs.empty()) { return; }

    auto entry = CSiTRadar::mAcData.find(cs);
    if (entry == CSiTRadar::mAcData.end()) { return; }

    if (entry->second.pointOutFromMe && !strcmp(msg.c_str(), "OK")) {
        entry->second.POAcceptTime = clock();
    }
}
