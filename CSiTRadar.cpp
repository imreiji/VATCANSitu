#include "pch.h"
#include "CSiTRadar.h"
#include "PositionString.h"

using namespace Gdiplus;

// Initialize Static Members
unordered_map<string, ACData> CSiTRadar::mAcData;
unordered_map<string, int> CSiTRadar::tempTagData;
map<string, menuButton> TopMenu::menuButtons;
unordered_map<string, clock_t> CSiTRadar::hoAcceptedTime;
buttonStates CSiTRadar::menuState = {};
bool CSiTRadar::halfSecTick = FALSE;
CRadarScreen* CSiTRadar::m_pRadScr;
unordered_map<int, ACList> acLists;
SituTbs::Config CSiTRadar::tbsConfig;
SituCpdlcStations::Table CSiTRadar::cpdlcStations;
SituCpdlcFreetext::Table CSiTRadar::cpdlcFreetext;
unordered_map<string, bool> CSiTRadar::acADSB;
unordered_map<string, bool> CSiTRadar::acRVSM;
std::shared_mutex CSiTRadar::acCapabilityMutex;
CSiTRadar::SCPDLCPollResult CSiTRadar::cpdlcPollResult;
std::mutex CSiTRadar::cpdlcPollMutex;
bool CSiTRadar::cpdlcPollInFlight = false;

void CSiTRadar::StartCPDLCPoll()
{
	{
		std::lock_guard<std::mutex> lock(cpdlcPollMutex);
		if (cpdlcPollInFlight) { return; }
		cpdlcPollInFlight = true;
	}

	std::thread poll([]() {
		std::string raw = CPDLCMessage::PollCPDLCMessages();

		SCPDLCPollResult result;

		if (raw.compare(0, 2, "ok") != 0) {
			result.error = "Hoppie Error, Try Reconnecting Error:" + raw;
		}
		else if (raw.compare(0, 3, "ok ") == 0) {
			// parseDLMessage consumes from the front of raw, and now always shortens it,
			// so this terminates even on a malformed reply.
			while (raw.length() > 4) {
				result.messages.push_back(CPDLCMessage::parseDLMessage(raw));
			}
		}

		result.ready = true;

		// A peek only applies to the first fetch after logon; later fetches poll, which
		// consumes. Flipped here on the worker rather than at the call site, because the
		// flag is read twice during one fetch - once to build the URL, and again by
		// parseDLMessage, which strips the leading Hoppie message id only in peek mode - so
		// it must not change between those two points. Upstream flips it in the button
		// handler and never sets it back, so a logoff and logon in the same session skipped
		// the peek entirely; the logon handler now re-arms it.
		CPDLCMessage::firstPeek = false;

		std::lock_guard<std::mutex> lock(cpdlcPollMutex);
		cpdlcPollResult = result;
		cpdlcPollInFlight = false;
		});

	poll.detach();
}

void CSiTRadar::DrainCPDLCPoll()
{
	SCPDLCPollResult result;

	{
		std::lock_guard<std::mutex> lock(cpdlcPollMutex);
		if (!cpdlcPollResult.ready) { return; }
		result.messages.swap(cpdlcPollResult.messages);
		result.error.swap(cpdlcPollResult.error);
		cpdlcPollResult.ready = false;
	}

	// Everything below touches the EuroScope SDK or mAcData, so it only runs here.
	if (!result.error.empty()) {
		menuState.CPDLCOn = false;
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Hoppie CPDLC", result.error.c_str(),
			true, false, false, false, false);
		return;
	}

	for (auto& message : result.messages) {
		auto aircraft = mAcData.find(message.sender);
		if (aircraft == mAcData.end()) { continue; }

		aircraft->second.CPDLCMessages.push_back(message);

		if (message.opensMnemonic) {
			aircraft->second.cpdlcMnemonic = true;
		}
		// Connection state. REQUEST LOGON is how an aircraft asks; LOGOFF is how it
		// leaves. Until now this field was written to zero in one place and read
		// nowhere, so the window could not tell an aircraft that had asked to connect
		// from one that never had.
		if (message.rawMessageContent == "REQUEST LOGON") {
			if (aircraft->second.cpdlcState != CPDLC_CONNECTED) {
				aircraft->second.cpdlcState = CPDLC_LOGON_REQUESTED;
			}
		}
		else if (message.rawMessageContent == "LOGOFF") {
			aircraft->second.cpdlcState = CPDLC_NOT_CONNECTED;
		}
	}
}

CSiTRadar::CSiTRadar()
{
	m_pRadScr = this;

	halfSec = clock();
	halfSecTick = FALSE;

	menuState.haloRad = 5;
	menuState.ptlTool = FALSE;
	menuLayer = 0;

#pragma region intializeLists
	//initialize lists
	// Defaults are offsets from the top left of the radar area, not screen coordinates.
	// The y values clear the 60 pixel header strip.
	ACList atisList, offScreenList, messageList;
	atisList.listType = LIST_TIME_ATIS;
	atisList.offset = { 500, 84 };
	offScreenList.listType = LIST_OFF_SCREEN;
	offScreenList.offset = { 0, 500 };
	messageList.listType = LIST_MESSAGES;
	messageList.offset = { 800, 84 };
	acLists[LIST_TIME_ATIS] = atisList;
	acLists[LIST_OFF_SCREEN] = offScreenList;
	acLists[LIST_MESSAGES] = messageList;
#pragma endregion

	// load settings file
	try {
		if (menuState.ctrlRemarkDefaults.size() < 7) {
			for (int i = (int)menuState.ctrlRemarkDefaults.size(); i < 7; i++) {
				menuState.ctrlRemarkDefaults.emplace_back("");
			}
		}

		// TBS configuration, read once. A missing file leaves no airports configured,
		// which turns the marker off rather than breaking anything.
		{
			const std::string tbsPath = wxRadar::getSituWxDir() + "SituTBS.txt";
			std::ifstream tbsFile(tbsPath.c_str(), std::ios::binary);
			if (tbsFile.is_open()) {
				std::stringstream tbsText;
				tbsText << tbsFile.rdbuf();
				const SituConfig::ParseResult tbsParsed = SituConfig::Parse(tbsText.str());
				tbsConfig = SituTbs::Parse(tbsParsed);

				// Say what was thrown away. A separation figure that failed to load is
				// the worst thing in that file to be quiet about.
				const size_t bad = tbsParsed.skippedLines.size() + tbsConfig.rejectedLines.size();
				if (bad != 0) {
					GetPlugIn()->DisplayUserMessage("VATCAN Situ", "TBS",
						("SituTBS.txt: " + std::to_string(bad) + " line(s) not understood and ignored").c_str(),
						true, true, false, false, false);
				}
			}
		}

		// CPDLC station table. Absent, every next data authority lookup fails and says
		// so, which is the right outcome - it never invents a station.
		{
			const std::string cpdlcPath = wxRadar::getSituWxDir() + "SituCPDLC.txt";
			if (SituFiles::Exists(cpdlcPath)) {
				const SituConfig::ParseResult cpdlcParsed = SituConfig::Parse(SituFiles::Read(cpdlcPath));
				cpdlcStations = SituCpdlcStations::Parse(cpdlcParsed);
				CPDLCMessage::dclTable = SituCpdlcDcl::Parse(cpdlcParsed);
				cpdlcFreetext = SituCpdlcFreetext::Parse(cpdlcParsed);

				if (!cpdlcStations.skippedLines.empty()) {
					GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
						("SituCPDLC.txt: " + std::to_string(cpdlcStations.skippedLines.size())
							+ " line(s) not understood and ignored").c_str(),
						true, true, false, false, false);
				}
				for (const std::string& duplicate : cpdlcStations.duplicateControllerIds) {
					GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
						("SituCPDLC.txt: controller id " + duplicate
							+ " is claimed by more than one station; the callsign prefix decides").c_str(),
						true, true, false, false, false);
				}
			}
		}

		// Settings live beside the DLL, resolved against the module rather than the
		// process working directory. ".\situWx\" resolved against EuroScope's, and any
		// file dialog moves that, which is how these folders ended up scattered.
		const std::string settingsPath = wxRadar::getSituWxDir() + "settings.txt";
		const std::string localPath = wxRadar::getSituWxDir() + "SituLocal.txt";
		const std::string legacyPath = wxRadar::getSituWxDir() + "settings.json";

		SituSettings::Settings settings;
		SituSettings::LocalSettings local;
		bool migratedFromJson = false;

		if (SituFiles::Exists(settingsPath)) {
			settings = SituSettings::Parse(SituConfig::Parse(SituFiles::Read(settingsPath)));
		}
		else if (SituFiles::Exists(legacyPath)) {
			// One time migration, and the last time this file is read at all.
			//
			// json::parse throws json::parse_error, which does not derive from
			// ifstream::failure - the type the old code caught - so a malformed file threw
			// out of this constructor, which EuroScope calls from OnRadarScreenCreated. An
			// exception leaving an SDK callback unwinds through frames built by another
			// compiler in a separately linked binary. Caught by base type here.
			try {
				settings = SituLegacy::SettingsFromJson(SituFiles::Read(legacyPath), local);
				migratedFromJson = true;
			}
			catch (const std::exception&) {
				GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Settings",
					"settings.json could not be read; starting from defaults",
					true, true, false, false, false);
			}
		}

		if (SituFiles::Exists(localPath)) {
			local = SituSettings::ParseLocal(SituConfig::Parse(SituFiles::Read(localPath)));
		}

		wxRadar::wxLatCtr = settings.wxLat;
		wxRadar::wxLongCtr = settings.wxLong;

		acLists[LIST_TIME_ATIS].offset = { settings.atisListOffset.x, settings.atisListOffset.y };
		acLists[LIST_OFF_SCREEN].offset = { settings.offScreenListOffset.x, settings.offScreenListOffset.y };
		acLists[LIST_MESSAGES].offset = { settings.messageListOffset.x, settings.messageListOffset.y };

		menuState.numHistoryDots = settings.historyDots;
		menuState.bigACID = settings.bigACID;
		menuState.wxAll = settings.wxAll;
		menuState.filterBypassAll = settings.filterBypassAll;
		menuState.extAltToggle = settings.extAltToggle;
		menuState.SFIPrefStringDefault = settings.prefSFI;

		for (size_t i = 0; i < menuState.ctrlRemarkDefaults.size() && i < settings.controllerRemarks.size(); i++) {
			menuState.ctrlRemarkDefaults[i] = settings.controllerRemarks[i];
		}

		CPDLCMessage::cpdlcServer = settings.cpdlcServer;
		CPDLCMessage::hoppieCode = local.hoppieCode;
		CPDLCMessage::hoppieICAO = local.hoppieICAO;

		// Say what was thrown away rather than dropping it quietly.
		const size_t badLines = settings.skippedLines.size() + local.skippedLines.size();
		if (badLines != 0) {
			GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Settings",
				(std::to_string(badLines) + " settings line(s) not understood and ignored").c_str(),
				true, true, false, false, false);
		}

		if (migratedFromJson) {
			GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Settings",
				"settings.json migrated to settings.txt; the logon code moved to SituLocal.txt",
				true, true, false, false, false);
		}
	}
	catch (const std::exception& e) {
		// Nothing above should throw: the tokeniser and the settings parsers do not, and
		// the one remaining JSON read carries its own handler. This is the backstop.
		//
		// It catches std::exception rather than std::ifstream::failure, which is what was
		// here before and is a type that neither json::parse_error nor json::type_error
		// derives from - so the handler that looked like it covered this never did. A
		// constructor called from OnRadarScreenCreated must not let anything out: an
		// exception leaving an SDK callback unwinds through frames built by a different
		// compiler in a separately linked binary.
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Settings",
			(string("settings load failed: ") + e.what()).c_str(),
			true, true, false, false, false);
	}

	try {
		if ( (((clock() - menuState.lastWxRefresh) / CLOCKS_PER_SEC) > 600 && (menuState.wxAll || menuState.wxHigh)) ||
			menuState.lastWxRefresh == 0) {
			// These were std::async with named futures, whose destructors block at the end
			// of this scope - so they never ran concurrently with anything. All they added
			// was a worker thread calling DisplayUserMessage, which is EuroScope SDK and
			// has no thread-safety guarantee. Call them directly: identical timing, no
			// cross-thread SDK use. (parseRadarPNG calls GetRainViewerJSON itself, so the
			// separate call here was redundant.)
			wxRadar::parseRadarPNG(this);
			menuState.lastWxRefresh = clock();
		}
		// on intial load, only do once so that asr loading is not slowed (update will happen "on refresh" afterwards)
		if (menuState.lastMetarRefresh == 0) {

			std::thread tc(wxRadar::parseVatsimMetar, 0);
			tc.detach();
			//std::future<void> fc = std::async(std::launch::async, wxRadar::parseVatsimMetar, 0);
			menuState.lastMetarRefresh = clock();
		}
		if (menuState.lastAtisRefresh == 0) {
			std::thread td(wxRadar::parseVatsimATIS, 0);
			td.detach();
			//std::future<void> fd = std::async(std::launch::async, wxRadar::parseVatsimATIS, 0);
			menuState.lastAtisRefresh = clock();
		}
	}
	catch (...) {
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "WX Parser", string("PNG Failed to Parse").c_str(), true, false, false, false, false);
	}

	CSiTRadar::mAcData.reserve(256);

	time = clock();
	oldTime = clock();
}

CSiTRadar::~CSiTRadar()
{
	// Save settings. Written every time rather than only when a file already existed:
	// the old code opened the file for reading first and skipped the whole save if that
	// failed, so a fresh install never persisted anything at all.
	SituSettings::Settings settings;
	settings.wxLat = wxRadar::wxLatCtr;
	settings.wxLong = wxRadar::wxLongCtr;

	settings.atisListOffset = { acLists[LIST_TIME_ATIS].offset.x, acLists[LIST_TIME_ATIS].offset.y };
	settings.offScreenListOffset = { acLists[LIST_OFF_SCREEN].offset.x, acLists[LIST_OFF_SCREEN].offset.y };
	settings.messageListOffset = { acLists[LIST_MESSAGES].offset.x, acLists[LIST_MESSAGES].offset.y };

	settings.historyDots = menuState.numHistoryDots;
	settings.bigACID = menuState.bigACID;
	settings.wxAll = menuState.wxAll;
	settings.filterBypassAll = menuState.filterBypassAll;
	settings.extAltToggle = menuState.extAltToggle;
	settings.prefSFI = menuState.SFIPrefStringDefault;
	settings.cpdlcServer = CPDLCMessage::cpdlcServer;

	for (size_t i = 0; i < settings.controllerRemarks.size() && i < menuState.ctrlRemarkDefaults.size(); i++) {
		settings.controllerRemarks[i] = menuState.ctrlRemarkDefaults[i];
	}

	SituSettings::LocalSettings local;
	local.hoppieCode = CPDLCMessage::hoppieCode;
	local.hoppieICAO = CPDLCMessage::hoppieICAO;

	// The folder may not exist yet; the weather path is what usually creates it.
	CreateDirectoryA(wxRadar::getSituWxDir().c_str(), NULL);

	SituFiles::Write(wxRadar::getSituWxDir() + "settings.txt", SituSettings::Serialize(settings));

	// Only written once there is something to write, so an install that has never used
	// CPDLC does not grow a file whose only purpose is to hold a credential.
	if (!local.hoppieCode.empty() || !local.hoppieICAO.empty()) {
		SituFiles::Write(wxRadar::getSituWxDir() + "SituLocal.txt", SituSettings::SerializeLocal(local));
	}

	m_pRadScr = nullptr;
}

void CSiTRadar::OnRefresh(HDC hdc, int phase)
{
	if (m_pRadScr != this) {
		m_pRadScr = this;

		const char* sfi;
		if ((sfi = GetDataFromAsr("prefSFI")) != NULL) {
			menuState.SFIPrefStringASRSetting = sfi;
		}
		else {
			menuState.SFIPrefStringASRSetting = menuState.SFIPrefStringDefault;
		}
	}

	// if no windows are open, there can be no focused textfield
	if (menuState.radarScrWindows.empty()) {
		menuState.focusedItem.m_focus_on = false;
	}
	else {
	// see if any focused items are set
		menuState.focusedItem.m_focus_on = false;
		for (auto& win : menuState.radarScrWindows) {
			for (auto& tf : win.second.m_textfields_) {
				if(tf.m_focused) {
					SetFocusedTextField(win.first, tf.m_textFieldID);
				}
			}
		}
	}

	// get cursor position and screen info
	POINT p;

	if (GetCursorPos(&p)) {
		if (ScreenToClient(GetActiveWindow(), &p)) {}
	}

	RECT radarea = GetRadarArea();

	// Get threaded messages. Take the whole queue under the lock, then display outside it:
	// this used to iterate the shared vector while worker threads could push_back into it,
	// which reallocates and invalidates the iterator being walked here.
	for (const auto& message : wxRadar::TakeAsyncMessages()) {
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Warning", message.reponseMessage.c_str(), true, false, false, false, false);
	}

#pragma region timers
	// time based functions
	double time = ((double)clock() - (double)halfSec) / ((double)CLOCKS_PER_SEC);
	if (time >= 0.5) {
		halfSec = clock();
		halfSecTick = !halfSecTick;
	}

	if (phase == REFRESH_PHASE_BEFORE_TAGS) {
		if (((clock() - menuState.lastWxRefresh) / CLOCKS_PER_SEC) > 600 && (menuState.wxAll || menuState.wxHigh)) {

			// autorefresh weather download every 10 minutes.
			// Direct call - see the constructor: the future's destructor blocked anyway,
			// so this was already synchronous, just on a thread that must not touch the
			// EuroScope SDK.
			wxRadar::parseRadarPNG(this);
			menuState.lastWxRefresh = clock();
		}

		// Kick off a Hoppie poll about once a minute. StartCPDLCPoll returns immediately;
		// DrainCPDLCPoll below picks the results up on a later refresh.
		if (menuState.CPDLCOn && ((clock() - menuState.lastCPDLCPoll) / CLOCKS_PER_SEC) > 60) {
			StartCPDLCPoll();
			menuState.lastCPDLCPoll = clock();
		}

		DrainCPDLCPoll();

		if (((clock() - menuState.lastMetarRefresh) / CLOCKS_PER_SEC) > 600) { // update METAR every 10 mins
			std::thread tc(wxRadar::parseVatsimMetar, 0);
			tc.detach();
			// fc = std::async(std::launch::async, wxRadar::parseVatsimMetar, 0);
			menuState.lastMetarRefresh = clock();
		}

		if (((clock() - menuState.lastAtisRefresh) / CLOCKS_PER_SEC) > 120) { // update ATIS letter every 2 mins
			std::thread td(wxRadar::parseVatsimATIS, 0);
			td.detach();
			// fd = std::async(std::launch::async, wxRadar::parseVatsimATIS, 0);
			menuState.lastAtisRefresh = clock();
		}

		if (((clock() - menuState.handoffModeStartTime) / CLOCKS_PER_SEC) > 10 && menuState.handoffMode) {
			menuState.handoffMode = FALSE;
			menuState.SFIMode = false;
			CSiTRadar::menuState.jurisdictionIndex = 0;
			SituPlugin::SendKeyboardPresses({ 0x01 });
		}

		// Garbage collect every 5 minutes
		if (((clock() - menuState.lastAcListMaint) / CLOCKS_PER_SEC) > 300) {

			menuState.recentCallsignsSeen.clear();

			for (CRadarTarget radarTarget = GetPlugIn()->RadarTargetSelectFirst(); radarTarget.IsValid();
				radarTarget = GetPlugIn()->RadarTargetSelectNext(radarTarget))
			{

				string callSign = radarTarget.GetCallsign();

				// Get active radar targets for garbage cleaning
				menuState.recentCallsignsSeen.emplace_back(callSign);

			}

			auto it = mAcData.cbegin();

			while (it != mAcData.cend())
			{
				if (std::find(menuState.recentCallsignsSeen.begin(), menuState.recentCallsignsSeen.end(), it->first) == menuState.recentCallsignsSeen.end())
				{
					// supported in C++11
					it = mAcData.erase(it);
				}
				else {
					++it;
				}
			}

			auto squawkCodeIt = menuState.squawkCodes.begin();

			while (squawkCodeIt != menuState.squawkCodes.end())
			{
				if (std::find(menuState.recentCallsignsSeen.begin(), menuState.recentCallsignsSeen.end(), squawkCodeIt->fpcs) == menuState.recentCallsignsSeen.end())
				{
					squawkCodeIt = menuState.squawkCodes.erase(squawkCodeIt);
				}
				else {
					++squawkCodeIt;
				}
			}

			menuState.lastAcListMaint = clock();

		}
	}
#pragma endregion 

	// set up the drawing renderer
	CDC dc;
	dc.Attach(hdc);

	Graphics g(hdc);

	double pixnm = PixelsPerNM();

	// Check if ASR is an IFR file
	if (GetDataFromAsr("DisplayTypeName") != NULL) {
		string DisplayType = GetDataFromAsr("DisplayTypeName");

		// VFR asrs should show the NARDS displays
		if (strcmp(DisplayType.c_str(), "VFR") == 0) {
			if (phase == REFRESH_PHASE_AFTER_TAGS) {

				for (CRadarTarget radarTarget = GetPlugIn()->RadarTargetSelectFirst(); radarTarget.IsValid();
					radarTarget = GetPlugIn()->RadarTargetSelectNext(radarTarget))
				{
					
					string callSign = radarTarget.GetCallsign();
					CSiTRadar::mAcData[radarTarget.GetCallsign()].tagType = 1;
					bool isCorrelated = radarTarget.GetCorrelatedFlightPlan().IsValid();

					POINT p = ConvertCoordFromPositionToPixel(radarTarget.GetPosition().GetPosition());

					// Altitude filters
					if (!radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe() || strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), GetPlugIn()->ControllerMyself().GetPositionId()) == 0) {
						if (altFilterOn && radarTarget.GetPosition().GetPressureAltitude() < altFilterLow * 100 && !menuState.filterBypassAll) {
							continue;
						}

						if (altFilterOn && altFilterHigh > 0 && radarTarget.GetPosition().GetPressureAltitude() > altFilterHigh * 100 && !menuState.filterBypassAll) {
							continue;
						}
					}

					// draw PPS
					bool isVFR = mAcData[callSign].hasVFRFP;
					bool isRVSM = mAcData[callSign].isRVSM;
					bool isADSB = mAcData[callSign].isADSB;

					if ((!isCorrelated && !isADSB) || (radarTarget.GetPosition().GetRadarFlags() != 0 && isADSB && !isCorrelated)) {
						mAcData[callSign].tagType = 3; // sets this if RT is uncorr
					}
					else if (isCorrelated && mAcData[callSign].tagType == 3) { mAcData[callSign].tagType = 0; } // only sets once to go from uncorr to corr
					// then allows it to be opened closed etc

					COLORREF ppsColor;

					// logic for the color of the PPS
					if (radarTarget.GetPosition().GetRadarFlags() == 0) { ppsColor = C_PPS_YELLOW; }
					else if (radarTarget.GetPosition().GetRadarFlags() == 1 && !isCorrelated) { ppsColor = C_PPS_MAGENTA; }
					else if (!strcmp(radarTarget.GetPosition().GetSquawk(), "7600") || !strcmp(radarTarget.GetPosition().GetSquawk(), "7700")) { ppsColor = C_PPS_RED; }
					else if (isVFR && isCorrelated) { ppsColor = C_PPS_ORANGE; }
					else { ppsColor = C_PPS_YELLOW; }

					if (radarTarget.GetPosition().GetTransponderI() == TRUE && halfSecTick) { ppsColor = C_WHITE; }

					RECT prect = CPPS::DrawPPS(&dc, isCorrelated, isVFR, isADSB, isRVSM, radarTarget.GetPosition().GetRadarFlags(), ppsColor, radarTarget.GetPosition().GetSquawk(), p);
					AddScreenObject(AIRCRAFT_SYMBOL, callSign.c_str(), prect, FALSE, "");

					// display CJS
					if ((radarTarget.GetPosition().GetRadarFlags() >= 2 && isCorrelated) || CSiTRadar::mAcData[radarTarget.GetCallsign()].isADSB) {
						string CJS = GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetTrackingControllerId();

						COLORREF cjsColor = C_PPS_YELLOW;

						dc.SelectObject(CFontHelper::Euroscope14);
						dc.SetTextColor(cjsColor);

						RECT rectCJS;
						rectCJS.left = p.x - 6;
						rectCJS.right = p.x + 75;
						rectCJS.top = p.y - 18;
						rectCJS.bottom = p.y;

						dc.DrawText(CJS.c_str(), &rectCJS, DT_LEFT);

					}

					if (radarTarget.GetPosition().GetRadarFlags() != 0) {
						CACTag::DrawNARDSTag(&dc, this, &radarTarget, &radarTarget.GetCorrelatedFlightPlan(), &rtagOffset);
					}
				}

				// NARDS Menu
			}
		}


		// IFR asrs should display the CAATS QAB
		if (strcmp(DisplayType.c_str(), "IFR") == 0) {


			if (phase == REFRESH_PHASE_AFTER_TAGS) {

				// Draw the mouse halo before menu, so it goes behind it
				if (menuState.haloCursor == true) {
					HaloTool::drawHalo(&dc, p, menuState.haloRad, pixnm);
					SituPlugin::prevMouseDelta = 0; // sync refrehes
				}

				// Convert any position carried over from an older settings file, and keep
				// every list reachable if the radar area has shrunk since it was placed.
				ResolveListOffsets(radarea);

				DrawACList(ListOrigin(acLists[LIST_TIME_ATIS], radarea), &dc, mAcData, LIST_TIME_ATIS);
				DrawACList(ListOrigin(acLists[LIST_OFF_SCREEN], radarea), &dc, mAcData, LIST_OFF_SCREEN);
				DrawACList(ListOrigin(acLists[LIST_MESSAGES], radarea), &dc, mAcData, LIST_MESSAGES);

				


				for (CRadarTarget radarTarget = GetPlugIn()->RadarTargetSelectFirst(); radarTarget.IsValid();
					radarTarget = GetPlugIn()->RadarTargetSelectNext(radarTarget))
				{
					string callSign = radarTarget.GetCallsign();


					if (menuState.filterBypassAll) {
						mAcData[radarTarget.GetCallsign()].tagType = 1;
					}

					// Correlation check
					if (radarTarget.GetPosition().GetRadarFlags() > 1) {
						auto sqitr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&radarTarget](SSquawkCodeManagement& m)->bool {return !strcmp(m.squawk.c_str(), radarTarget.GetPosition().GetSquawk()); });

						if (radarTarget.GetCorrelatedFlightPlan().IsValid()) {

							if (sqitr == menuState.squawkCodes.end()) {

								radarTarget.Uncorrelate();

							}

						}
						else {
							if (sqitr != menuState.squawkCodes.end()) {

								int sqkc = atoi(radarTarget.GetPosition().GetSquawk());

								if (sqkc == 1000 || sqkc == 1200 || sqkc == 1400 || sqkc == 2000) {}
								else {

									if (sqitr->numCorrelatedRT == 0) {

										radarTarget.CorrelateWithFlightPlan(GetPlugIn()->FlightPlanSelect(sqitr->fpcs.c_str()));
										sqitr->numCorrelatedRT++;
										mAcData[callSign].multipleDiscrete = false;
									}

									else {

										// Multiple discrete offender handling, squawk should be forced on and it should flash, and it should not correlate
										radarTarget.Uncorrelate();
										mAcData[callSign].multipleDiscrete = true;

										// Reported by the message list, which reads this
										// flag back out of mAcData in DrawACList.
									}
								}
							}
						}
					}
					// A target without SSR must not carry a flight plan association: a primary
					// return is an unknown until a controller says otherwise, which is the
					// symbology this plugin exists to reproduce.
					//
					// This used to call Uncorrelate() on every refresh, guarded by a
					// manualCorr flag that nothing ever set to true. The guard was therefore
					// dead and the call unconditional. That mattered, because the SDK
					// documents Uncorrelate as "after uncorrelating the FP will not be
					// correlated automatically by the system" - so once the association is
					// broken EuroScope never restores it, and every later call had nothing
					// left to undo except a correlation the controller had just made by
					// hand. Manually correlating a primary target was impossible: the plugin
					// took it apart again within a frame.
					//
					// Break the automatic association once and then leave the target alone.
					// The flag is reset when the target regains SSR, so a code appearing and
					// disappearing re-arms it.
					const int radarFlags = radarTarget.GetPosition().GetRadarFlags();

					if (radarFlags >= 2) {
						mAcData[callSign].autoCorrelationCleared = false;
					}
					else if (radarFlags == 0) {
						// No radar at all. Never allow an association to stand, which is
						// what the original radarFlags == 0 clause said.
						radarTarget.Uncorrelate();
						mAcData[callSign].autoCorrelationCleared = false;
					}
					else if (!mAcData[callSign].autoCorrelationCleared) {
						radarTarget.Uncorrelate();
						mAcData[callSign].autoCorrelationCleared = true;
					}

					// altitude filtering 

					// Destination airport highlighting
					auto itr = std::find(begin(CSiTRadar::menuState.destICAO), end(CSiTRadar::menuState.destICAO), radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetDestination());
					bool isDest = false;

					if (itr != end(CSiTRadar::menuState.destICAO)
						&& strcmp(radarTarget.GetCorrelatedFlightPlan().GetFlightPlanData().GetDestination(), "") != 0) {
						if (CSiTRadar::menuState.destArptOn[distance(CSiTRadar::menuState.destICAO, itr)]) {
							isDest = true;
						}
					}

					if (!radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) // Filter the aircraft if this statement is true, i.e. I'm not tracking
					{
						if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), GetPlugIn()->ControllerMyself().GetPositionId()) != 0 &&
							strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") == 0) {

	

								if (altFilterOn && radarTarget.GetPosition().GetPressureAltitude() < altFilterLow * 100
									&& !menuState.filterBypassAll
									) {
									continue;
								}

								if (altFilterOn && altFilterHigh > 0 && radarTarget.GetPosition().GetPressureAltitude() > altFilterHigh * 100
									&& !menuState.filterBypassAll
									&& !isDest) {

									if (radarTarget.GetPosition().GetRadarFlags() != 1) { // can't filter primary targets will make it just respect the high limit, to avoid ground clutter
										continue;
									}

								}
							
						}
					}

					POINT p = ConvertCoordFromPositionToPixel(radarTarget.GetPosition().GetPosition());

					// Check if target is on the display area
					if (p.y < GetRadarArea().top ||
						p.y > GetRadarArea().bottom ||
						p.x < GetRadarArea().left ||
						p.x > GetRadarArea().right)
					{
						mAcData[radarTarget.GetCallsign()].isOnScreen = false;
					}
					else {
						mAcData[radarTarget.GetCallsign()].isOnScreen = true;
					}

					// Draw pending direct to line if exists
					if (mAcData[callSign].directToLineOn) {
						// Save/restore so the pen is deselected before it is deleted,
						// otherwise DeleteObject fails and the handle leaks each frame.
						int sDCDct = dc.SaveDC();

						HPEN targetPen;
						targetPen = CreatePen(PS_DASHDOT, 1, C_WHITE);
						dc.SelectObject(targetPen);

						dc.MoveTo(p);
						dc.LineTo(ConvertCoordFromPositionToPixel(mAcData[callSign].directToPendingPosition));

						RECT dctFixNameRect;
						dctFixNameRect.top = ConvertCoordFromPositionToPixel(mAcData[callSign].directToPendingPosition).y + 3;
						dctFixNameRect.left = ConvertCoordFromPositionToPixel(mAcData[callSign].directToPendingPosition).x -15;

						dc.DrawText(mAcData[callSign].directToPendingFixName.c_str(), &dctFixNameRect, DT_CENTER | DT_CALCRECT);
						dc.DrawText(mAcData[callSign].directToPendingFixName.c_str(), &dctFixNameRect, DT_CENTER);

						dc.RestoreDC(sDCDct);
						DeleteObject(targetPen);
					}

					// Get information about the Aircraft/Flightplan
					bool isCorrelated = radarTarget.GetCorrelatedFlightPlan().IsValid();
					bool isVFR = mAcData[callSign].hasVFRFP;
					bool isRVSM = mAcData[callSign].isRVSM;
					bool isADSB = mAcData[callSign].isADSB;

					// Draw PTL
					if (hasPTL.find(radarTarget.GetCallsign()) != hasPTL.end()) {

						HaloTool::drawPTL(&dc, radarTarget, this, p, menuState.ptlLength);
						
					}
					else if (menuState.ptlAll && radarTarget.GetPosition().GetRadarFlags() != 0) {
						if (radarTarget.GetPosition().GetRadarFlags() == 4 && !isADSB) {}
						else {
							HaloTool::drawPTL(&dc, radarTarget, this, p, menuState.ptlLength);
							
						}
					}
					else if ((CSiTRadar::menuState.ebPTL && radarTarget.GetPosition().GetReportedHeading() > 0 && radarTarget.GetPosition().GetReportedHeading() < 181) ||
						(CSiTRadar::menuState.wbPTL && radarTarget.GetPosition().GetReportedHeading() > 180 && radarTarget.GetPosition().GetReportedHeading() < 360)

						) {
						HaloTool::drawPTL(&dc, radarTarget, this, p, menuState.ptlLength);
					}

					// Draw TBS Marker
					//
					// Which airports have TBS, their magnetic variation, the approach
					// gate and the whole wake matrix now come from SituTBS.txt. All of
					// it used to be here, gated on the destination being CYYZ, with
					// Toronto's variation written as a bare 10 in two places with
					// opposite signs.
					{
						const CFlightPlan tbsFp = radarTarget.GetCorrelatedFlightPlan();
						const int clearedAltitude = tbsFp.GetControllerAssignedData().GetClearedAltitude();

						// 1 and 2 are the EuroScope conventions for cleared for approach.
						if (clearedAltitude == 1 || clearedAltitude == 2) {

							const string tbsDestination = tbsFp.GetFlightPlanData().GetDestination();
							const SituTbs::Airport* tbsAirport =
								SituTbs::FindAirport(tbsConfig, tbsDestination);

							const double distanceToDest = tbsFp.GetDistanceToDestination();

							if (tbsAirport != nullptr
								&& distanceToDest < tbsConfig.gate.maxDistanceNm
								&& distanceToDest > tbsConfig.gate.minDistanceNm
								&& radarTarget.GetPosition().GetPressureAltitude() > tbsConfig.gate.minAltitudeFt) {

								// Which runway the aircraft is established on. The course
								// comes from the runway's own thresholds, so it is true
								// already and the track can be compared to it directly -
								// this is what removed the hardcoded magnetic variation.
								//
								// A course typed into the menu narrows it to one runway
								// when several are active. That comparison is against the
								// designator, which is magnetic like the typed value, so
								// it needs no conversion either.
								double approachCourse = -1.0;

								for (const ActiveArrivalRunway& arrival : menuState.activeArrivalRunways) {
									if (arrival.airport != tbsDestination) { continue; }

									if (menuState.tbsHdg != 0) {
										int designatorDelta = (arrival.magneticDesignator - menuState.tbsHdg + 540) % 360 - 180;
										if (abs(designatorDelta) > 15) { continue; }
									}

									// Normalised to -180..180 so a track either side of
									// north is still a small difference: the raw
									// subtraction reads 358 against 5 as 363 rather than
									// -7, and TBS would silently refuse to engage on a
									// runway pointing near north.
									double headingDelta = radarTarget.GetTrackHeading() - arrival.trueCourse;
									headingDelta = fmod(headingDelta + 180.0, 360.0);
									if (headingDelta < 0) { headingDelta += 360.0; }
									headingDelta -= 180.0;

									if (fabs(headingDelta) < tbsConfig.gate.headingToleranceDeg) {
										approachCourse = arrival.trueCourse;
										break;
									}
								}

								if (approachCourse >= 0.0) {

									double tbsDist = 0;
									const bool haveSeparation = SituTbs::SeparationFor(
										tbsConfig,
										tbsFp.GetFlightPlanData().GetAircraftWtc(),
										SituTbs::WtcForFollowerIndex(mAcData[callSign].follower),
										radarTarget.GetGS(),
										menuState.tbsMixed,
										tbsDist);

									if (haveSeparation) {
										POINT followerP = HaloTool::drawTBS(&dc, radarTarget, this, p, tbsDist, pixnm,
											approachCourse);

										// draw letter to allow toggling of follower
										dc.SelectObject(CFontHelper::Euroscope14);
										dc.SetTextColor(C_PPS_TBS_PINK);

										RECT rectTBS;
										rectTBS.left = followerP.x + 5;
										rectTBS.right = followerP.x + 15;
										rectTBS.top = followerP.y - 5;
										rectTBS.bottom = followerP.y + 10;

										const string tbsFollowerStr(1, SituTbs::WtcForFollowerIndex(mAcData[callSign].follower));
										dc.DrawText(tbsFollowerStr.c_str(), &rectTBS, DT_LEFT);
										AddScreenObject(TBS_FOLLOWER_TOGGLE, callSign.c_str(), rectTBS, false, "Toggle TBS Follower");
									}
								}
							}
						}
					}



					if ((!isCorrelated && !isADSB) || (radarTarget.GetPosition().GetRadarFlags() != 0 && isADSB && !isCorrelated)) {
						mAcData[callSign].tagType = 3; // sets this if RT is uncorr
					}
					else if (isCorrelated && mAcData[callSign].tagType == 3) { mAcData[callSign].tagType = 0; } // only sets once to go from uncorr to corr
					// then allows it to be opened closed etc

					COLORREF ppsColor;

					// logic for the color of the PPS
					if (radarTarget.GetPosition().GetRadarFlags() == 0) { ppsColor = C_PPS_YELLOW; }
					else if (radarTarget.GetPosition().GetRadarFlags() == 1 ) { ppsColor = C_PPS_MAGENTA; }
					else if (!strcmp(radarTarget.GetPosition().GetSquawk(), "7600") || !strcmp(radarTarget.GetPosition().GetSquawk(), "7700")) { ppsColor = C_PPS_RED; }
					else if (isVFR && isCorrelated) { ppsColor = C_PPS_ORANGE; }
					else { ppsColor = C_PPS_YELLOW; }

					if (radarTarget.GetPosition().GetTransponderI() == TRUE && halfSecTick) { ppsColor = C_WHITE; }

					RECT prect = CPPS::DrawPPS(&dc, isCorrelated, isVFR, isADSB, isRVSM, radarTarget.GetPosition().GetRadarFlags(), ppsColor, radarTarget.GetPosition().GetSquawk(), p);
					AddScreenObject(AIRCRAFT_SYMBOL, callSign.c_str(), prect, FALSE, "");

					if (radarTarget.GetPosition().GetRadarFlags() != 0 && radarTarget.GetPosition().GetRadarFlags() !=4) {
						CACTag::DrawRTACTag(&dc, this, &radarTarget, &radarTarget.GetCorrelatedFlightPlan(), &rtagOffset);
						if (radarTarget.GetGS() > 10) {
							CACTag::DrawHistoryDots(&dc, &radarTarget);
						}
					}
					else if (radarTarget.GetPosition().GetRadarFlags() == 4 && isADSB) {
						CACTag::DrawRTACTag(&dc, this, &radarTarget, &radarTarget.GetCorrelatedFlightPlan(), &rtagOffset);
						if (radarTarget.GetGS() > 10) {
							CACTag::DrawHistoryDots(&dc, &radarTarget);
						}
					}
					
						// Tag Level Logic
					if (menuState.nearbyCJS.find(radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerId()) != menuState.nearbyCJS.end() &&
						menuState.nearbyCJS.at(radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerId())) {
						// Open tags for quick looked targets
						CSiTRadar::mAcData[radarTarget.GetCallsign()].tagType = 1;
						CSiTRadar::mAcData[radarTarget.GetCallsign()].isQuickLooked = true;
					}
					else if (menuState.nearbyCJS.find(radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerId()) != menuState.nearbyCJS.end() &&
						!menuState.nearbyCJS.at(radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerId())
						&& CSiTRadar::mAcData[radarTarget.GetCallsign()].isQuickLooked) {

						CSiTRadar::mAcData[radarTarget.GetCallsign()].tagType = 0;
						CSiTRadar::mAcData[radarTarget.GetCallsign()].isQuickLooked = false;
					}

					if (radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
						CSiTRadar::mAcData[radarTarget.GetCallsign()].tagType = 1; // alpha tag if you have jurisdiction over the aircraft
						CSiTRadar::mAcData[radarTarget.GetCallsign()].isJurisdictional = true;

						// if you are handing off to someone
						if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") != 0) {
							mAcData[callSign].isHandoff = TRUE;

							// Clear the entry used for pointout coordination
							radarTarget.GetCorrelatedFlightPlan().GetControllerAssignedData().SetFlightStripAnnotation(0, "");
						}
					}
					else {
						CSiTRadar::mAcData[radarTarget.GetCallsign()].isJurisdictional = false;
					}

					// Once the handoff is complete, 
					if (mAcData[callSign].isHandoff == TRUE && strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") == 0) {
						mAcData[callSign].isHandoff = FALSE;
						// record the time of handoff acceptance unless the handoff is recalled by yourself
						if (!radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
							hoAcceptedTime[callSign] = clock();

							// if jurisdiction changes, pointouts are cleared.
							// This must act on callSign - the aircraft whose handoff just
							// completed - not on whatever happens to be selected right now.
							// It previously used FlightPlanSelectASEL(), so completing any
							// handoff cleared the point-out annotation on the selected
							// aircraft and pushed its strip to that aircraft's POTarget.
							GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetControllerAssignedData().SetFlightStripAnnotation(1, "");
							SendPointOut(mAcData[callSign].POTarget.c_str(), "", &GetPlugIn()->FlightPlanSelect(callSign.c_str()));

							mAcData[callSign].pointOutFromMe = false;
							mAcData[callSign].pointOutToMe = false;
							mAcData[callSign].POTarget = "";
							mAcData[callSign].POString = "";
						}
					}

					// Post handoff blinking and then close the tag
					if (hoAcceptedTime.find(callSign) != hoAcceptedTime.end() && (clock() - hoAcceptedTime[callSign]) / CLOCKS_PER_SEC > 12) {
						hoAcceptedTime.erase(callSign);

						// if quick look is open, defer closing the tag until bypass All filter is off;
						if (!menuState.filterBypassAll) {
							mAcData[callSign].tagType = 0;
						}
						if (menuState.filterBypassAll) {
							tempTagData[callSign] = 0;
						}
					}

					// Once the handoff to me is completed,
					if (mAcData[callSign].isHandoffToMe == TRUE && strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") == 0) {
						mAcData[callSign].isHandoffToMe = FALSE;
					}

					// Open a bravo tag, during a handoff to you
					if (strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), GetPlugIn()->ControllerMyself().GetPositionId()) == 0 &&
						strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") != 0) {
						CSiTRadar::mAcData[radarTarget.GetCallsign()].tagType = 1;
						mAcData[callSign].isHandoffToMe = TRUE;
					}

					// Handoff warning system: if the plane is within 2 minutes of exiting your airspace, CJS will blink

					COLORREF cjsColor = C_PPS_YELLOW;

					if (radarTarget.GetCorrelatedFlightPlan().GetTrackingControllerIsMe()) {
						if (radarTarget.GetCorrelatedFlightPlan().GetSectorExitMinutes() <= 2
							&& radarTarget.GetCorrelatedFlightPlan().GetSectorExitMinutes() >= 0
							&& halfSecTick == TRUE
							&& strcmp(radarTarget.GetCorrelatedFlightPlan().GetHandoffTargetControllerId(), "") == 0) {
							cjsColor = C_WHITE;
						}
					}

					// show CJS for controller tracking aircraft // or if in handoff mode, show the target controller's CJS
					if ((radarTarget.GetPosition().GetRadarFlags() >= 2 && isCorrelated)) { // || CSiTRadar::mAcData[radarTarget.GetCallsign()].isADSB) {
						if (radarTarget.GetPosition().GetRadarFlags() == 4 && !isADSB) {}
						else {

							dc.SelectObject(CFontHelper::Euroscope14);
							dc.SetTextColor(cjsColor);

							RECT rectCJS;
							rectCJS.left = p.x - 8;
							rectCJS.right = p.x + 75;
							rectCJS.top = p.y - 20;
							rectCJS.bottom = p.y;

							string CJS = GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetTrackingControllerId();

							if (menuState.handoffMode &&
								strcmp(radarTarget.GetCallsign(), GetPlugIn()->FlightPlanSelectASEL().GetCallsign()) == 0) {
								CJS = GetPlugIn()->ControllerSelect(GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetCoordinatedNextController()).GetPositionId();
								dc.SetTextColor(C_WHITE);
							}

							dc.DrawText(CJS.c_str(), &rectCJS, DT_LEFT);

							dc.SetTextColor(cjsColor);
						}
					}

					// plane halo looks at the <map> hashalo to see if callsign has a halo, if so, draws halo
					if (hashalo.find(radarTarget.GetCallsign()) != hashalo.end()) {
						HaloTool::drawHalo(&dc, p, menuState.haloRad, pixnm);
					}


					// Draw the Selected Aircraft Box
					HPEN targetPen;
					RECT selectBox{};
					selectBox.left = p.x - 6;
					selectBox.right = p.x + 7;
					selectBox.top = p.y - 6;
					selectBox.bottom = p.y + 7;

					// Save/restore so the pen is deselected before it is deleted below.
					int sDCSelBox = dc.SaveDC();

					targetPen = CreatePen(PS_SOLID, 1, C_WHITE);
					dc.SelectObject(targetPen);
					dc.SelectStockObject(NULL_BRUSH);

					dc.SelectObject(CFontHelper::Euroscope14);
					dc.SetTextColor(C_WHITE);


					if (CSiTRadar::menuState.handoffMode || CSiTRadar::menuState.SFIMode) {
						if (strcmp(radarTarget.GetCallsign(), GetPlugIn()->FlightPlanSelectASEL().GetCallsign()) == 0) {

							dc.Rectangle(&selectBox);

							RECT rectHO;
							rectHO.left = p.x - 16;
							rectHO.right = p.x + 20;
							rectHO.top = p.y + 8;
							rectHO.bottom = p.y + 28;
							if (menuState.handoffMode) {
								dc.DrawText("H/O", &rectHO, DT_CENTER);
							}
							if (menuState.SFIMode) {
								dc.DrawText("SFI", &rectHO, DT_CENTER);
							}

						}
					}
					else if (mAcData[callSign].pointOutToMe){

						if (mAcData[callSign].pointOutPendingApproval) { // change to flashing for point out events;

								dc.Rectangle(&selectBox);
								RECT rectHighlight;
								string POString, POString2;
								POString = "P/Out " + mAcData[callSign].POTarget;
								POString2 = mAcData[callSign].POString;

								rectHighlight.left = p.x - 9;
								rectHighlight.right = p.x + 20;
								rectHighlight.top = p.y + 8;
								rectHighlight.bottom = p.y + 28;
								AddScreenObject(HIGHLIGHT_POINT_OUT_ACCEPT, callSign.c_str(), rectHighlight, false, "Accept Point Out");

								dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);

								if (halfSecTick) {
									dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT);
								}
								rectHighlight.top = rectHighlight.bottom - 3;

								dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);
								if (halfSecTick) {
									dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT);
								}
								AddScreenObject(HIGHLIGHT_POINT_OUT_ACCEPT, callSign.c_str(), rectHighlight, false, "Accept Point Out");

								
						}
						else {
							dc.Rectangle(&selectBox);
							RECT rectHighlight;
							string POString, POString2;
							POString = "P/Out " + mAcData[callSign].POTarget;
							POString2 = mAcData[callSign].POString;

							rectHighlight.left = p.x - 9;
							rectHighlight.right = p.x + 20;
							rectHighlight.top = p.y + 8;
							rectHighlight.bottom = p.y + 28;

							dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);
							dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT);
							rectHighlight.top = rectHighlight.bottom - 3;

							dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);
							dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT);
						}
					}
					else if (mAcData[callSign].pointOutFromMe) {

						dc.Rectangle(&selectBox);

						if ( ((clock() - mAcData[callSign].POAcceptTime) / CLOCKS_PER_SEC) < 8 &&
							((clock() - mAcData[callSign].POAcceptTime) / CLOCKS_PER_SEC) > 0 &&
							halfSecTick) {

							bool i = true;

						} else {

							RECT rectHighlight;
							string POString, POString2;
							POString = "P/Out " + mAcData[callSign].POTarget;
							POString2 = mAcData[callSign].POString;

							rectHighlight.left = p.x - 9;
							rectHighlight.right = p.x + 20;
							rectHighlight.top = p.y + 8;
							rectHighlight.bottom = p.y + 28;

							dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);
							dc.DrawText(POString.c_str(), &rectHighlight, DT_LEFT);
							rectHighlight.top = rectHighlight.bottom - 3;

							dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT | DT_CALCRECT);
							dc.DrawText(POString2.c_str(), &rectHighlight, DT_LEFT);
						}
					}

					dc.RestoreDC(sDCSelBox);
					DeleteObject(targetPen);
				}

				// Flight plan loop. Goes through flight plans, and if not correlated will display
				if (menuState.showExtrapFP == TRUE) {
					for (CFlightPlan flightPlan = GetPlugIn()->FlightPlanSelectFirst(); flightPlan.IsValid();
						flightPlan = GetPlugIn()->FlightPlanSelectNext(flightPlan)) {
	
						if (flightPlan.GetCorrelatedRadarTarget().IsValid()) { continue; }
	
						// if the flightplan does not have a correlated radar target
						if (flightPlan.GetFPState() == FLIGHT_PLAN_STATE_SIMULATED) {
	
							// Store the points for history dots, only store new if position updated
							if (mAcData[flightPlan.GetCallsign()].prevPosition.empty()) {
	
								mAcData[flightPlan.GetCallsign()].prevPosition.push_back(flightPlan.GetFPTrackPosition().GetPosition());
	
							}
	
							else {
								if ((flightPlan.GetFPTrackPosition().GetPosition().m_Latitude != mAcData[flightPlan.GetCallsign()].prevPosition.back().m_Latitude) &&
									(flightPlan.GetFPTrackPosition().GetPosition().m_Longitude != mAcData[flightPlan.GetCallsign()].prevPosition.back().m_Longitude)) {
	
									if (static_cast<int>(mAcData[flightPlan.GetCallsign()].prevPosition.size()) < menuState.numHistoryDots) {
	
										mAcData[flightPlan.GetCallsign()].prevPosition.push_back(flightPlan.GetFPTrackPosition().GetPosition());
	
									}
									else {
	
										mAcData[flightPlan.GetCallsign()].prevPosition.pop_front();
										mAcData[flightPlan.GetCallsign()].prevPosition.push_back(flightPlan.GetFPTrackPosition().GetPosition());
									}
								}
							}
	
							CACTag::DrawFPACTag(&dc, this, &flightPlan.GetCorrelatedRadarTarget(), &flightPlan, &fptagOffset);
							CACTag::DrawFPConnector(&dc, this, &flightPlan.GetCorrelatedRadarTarget(), &flightPlan, C_PPS_ORANGE, &fptagOffset);
							CACTag::DrawHistoryDots(&dc, &flightPlan);
	
							POINT p = ConvertCoordFromPositionToPixel(flightPlan.GetFPTrackPosition().GetPosition());
	
							// draw the orange airplane symbol (credits andrewogden1678)
							GraphicsContainer gCont;
							gCont = g.BeginContainer();
	
							// Airplane icon 
	
							Point points[19] = {
								Point(0,-6),
								Point(-1,-5),
								Point(-1,-2),
								Point(-8,3),
								Point(-8,4),
								Point(-1,2),
								Point(-1,6),
								Point(-4,8),
								Point(-4,9),
								Point(0,8),
								Point(4,9),
								Point(4,8),
								Point(1,6),
								Point(1,2),
								Point(8,4),
								Point(8,3),
								Point(1,-2),
								Point(1,-5),
								Point(0,-6)
							};
	
							g.RotateTransform((REAL)flightPlan.GetFPTrackPosition().GetReportedHeading());
							g.TranslateTransform((REAL)p.x, (REAL)p.y, MatrixOrderAppend);
	
							SolidBrush orangeBrush(Color(255, 242, 120, 57));
	
							g.FillPolygon(&orangeBrush, points, 19);
							g.EndContainer(gCont);

							// orangeBrush is a GDI+ SolidBrush, not a GDI handle. It was
							// being passed to the Win32 DeleteObject, which took the
							// address of a C++ stack object as an HGDIOBJ. It destructs on
							// its own at the end of this scope.
						}
	
					}
				}

			}

			if (phase == REFRESH_PHASE_AFTER_LISTS) {

				// Free Text
				dc.SelectObject(CFontHelper::Euroscope14);
				dc.SetTextColor(C_PPS_YELLOW);


				for (auto& t : menuState.freetext) {
					RECT r;
					r.left = ConvertCoordFromPositionToPixel(t.m_pos).x;
					r.top = ConvertCoordFromPositionToPixel(t.m_pos).y;
					dc.DrawText(t.m_freetext_string.c_str(), &r, DT_LEFT | DT_CALCRECT);
					dc.DrawText(t.m_freetext_string.c_str(), &r, DT_LEFT);
					string txt;
					txt = "Free Text" + to_string(t.m_id);
					AddScreenObject(FREE_TEXT, to_string(t.m_id).c_str(), r, true, txt.c_str());
				}

				//

				// by reference: DrawWindow() writes hit-test rects back into the window's
				// buttons/listboxes/textfields. Iterating by value deep-copied every window
				// each frame and discarded those writes.
				for (auto& window : menuState.radarScrWindows) {

					// The CPDLC list is built from the message store when the window is
					// constructed, so without this a window left open never shows anything
					// that arrived after it was opened.
					if (window.second.m_winType == WINDOW_CPDLC && !window.second.m_listboxes_.empty()) {
						const auto entry = mAcData.find(window.second.m_callsign);
						if (entry != mAcData.end()) {
							window.second.m_listboxes_.front().PopulateCPDLCListBox(entry->second.CPDLCMessages);
						}
					}

					SWindowElements r = window.second.DrawWindow(&dc);
					AddScreenObject(WINDOW_TITLE_BAR, to_string(window.second.m_windowId_).c_str(), r.titleBarRect, true, to_string(window.second.m_windowId_).c_str());
					
					for (auto &elem : window.second.m_buttons_) {
						string windowFuncStr;
						windowFuncStr = to_string(elem.windowID) + " " + elem.text;
						AddScreenObject(window.second.m_winType, windowFuncStr.c_str(), elem.m_WindowButtonRect, true, windowFuncStr.c_str());
					}

					for (auto& listbox : window.second.m_listboxes_) {
						for (auto& lbE : listbox.listBox_) {
							string windowFuncStr;
							windowFuncStr = to_string(window.second.m_windowId_) + " " + to_string(lbE.m_elementID);
							AddScreenObject(WINDOW_LIST_BOX_ELEMENT, windowFuncStr.c_str(), lbE.m_ListBoxRect, true, windowFuncStr.c_str());
						}
						string lbFuncStr;
						lbFuncStr = to_string(window.second.m_windowId_) + " " + to_string(listbox.m_ListBoxID);
						AddScreenObject(WINDOW_SCROLL_ARROW_UP, lbFuncStr.c_str(), listbox.m_scrbar.uparrow, false, (lbFuncStr + " Up").c_str());
						AddScreenObject(WINDOW_SCROLL_ARROW_DOWN, lbFuncStr.c_str(), listbox.m_scrbar.downarrow, false, (lbFuncStr + " Down").c_str());
					}

					for (auto& tf : window.second.m_textfields_) {
						string windowFuncStr;
						windowFuncStr = to_string(window.second.m_windowId_) + " " + to_string(tf.m_textFieldID);
						AddScreenObject(WINDOW_TEXT_FIELD, windowFuncStr.c_str(), tf.m_textRect, true, windowFuncStr.c_str());
					}
					
					
				}

				if (menuState.MB3menu) {
					if (menuState.MB3menuType == 0) {
						CPopUpMenu acPopup(menuState.MB3clickedPt, &GetPlugIn()->FlightPlanSelect(GetPlugIn()->FlightPlanSelectASEL().GetCallsign()), m_pRadScr);
						acPopup.populateMenu();
						acPopup.m_origin.y += (acPopup.m_listElements.size() * 20);
						if (acPopup.m_origin.y > radarea.bottom) { acPopup.m_origin.y = radarea.bottom; }
						if ((acPopup.m_origin.x + 120) > radarea.right) { acPopup.m_origin.x = radarea.right - 120; }
						acPopup.drawPopUpMenu(&dc);
						for (auto& element : acPopup.m_listElements) {
							AddScreenObject(BUTTON_MENU_RMB_MENU, element.m_function.c_str(), element.elementRect, false, element.m_text.c_str());
						}
						acPopup.highlightSelection(&dc, menuState.MB3hoverRect);

						if (menuState.MB3SecondaryMenuOn) {
							acPopup.highlightSelection(&dc, menuState.MB3primRect); // Highlight the first level menu option
							CPopUpMenu secondaryMenu({ acPopup.totalRect.right, menuState.MB3primRect.bottom }, &GetPlugIn()->FlightPlanSelect(GetPlugIn()->FlightPlanSelectASEL().GetCallsign()), m_pRadScr);
							secondaryMenu.populateSecondaryMenu(menuState.MB3SecondaryMenuType);

							// Move back onto screen if necessary

							secondaryMenu.m_origin.y += ((int)secondaryMenu.m_listElements.size() * 20) / 2;
							if ((secondaryMenu.m_origin.y - ((int)secondaryMenu.m_listElements.size() * 20)) < (radarea.top + 60)) { secondaryMenu.m_origin.y = radarea.top + 65 + (secondaryMenu.m_listElements.size() * 20); }
							if (secondaryMenu.m_origin.y > GetChatArea().top) { secondaryMenu.m_origin.y = GetChatArea().top + 5; }
							// draw on other side of primary menu if it would be off the right side of the screen
							if (secondaryMenu.m_origin.x + secondaryMenu.m_width_ > radarea.right) {
								secondaryMenu.m_origin.x = acPopup.m_origin.x - (secondaryMenu.m_width_);
							}
							secondaryMenu.drawPopUpMenu(&dc);

							for (auto& element : secondaryMenu.m_listElements) {
								AddScreenObject(BUTTON_MENU_RMB_MENU_SECONDARY, element.m_function.c_str(), element.elementRect, false, element.m_text.c_str());
							}
							if (menuState.MB3hoverOn) {
								secondaryMenu.highlightSelection(&dc, menuState.MB3hoverRect);
							}
						}
					}
					if (menuState.MB3menuType == 1) {

						// Reserved for future expansion for other RMB clicks


					}
				}

				if (menuState.bgM3Click) {
					CPopUpMenu bgMenu(menuState.MB3clickedPt);

					bgMenu.m_listElements.emplace_back(SPopUpElement("Relocate", "relocate", 2, 0, 130));
					bgMenu.m_listElements.emplace_back(SPopUpElement("Free Text Clear All", "clrfreetext", 0, 0, 130));
					bgMenu.m_listElements.emplace_back(SPopUpElement("Free Text", "freetext", 2, 0, 130));
					bgMenu.m_listElements.emplace_back(SPopUpElement("Lat Long Clear", "llc", 0, 0, 130));
					bgMenu.m_listElements.emplace_back(SPopUpElement("Lat Long Readout", "llr", 0, 0, 130));
					bgMenu.m_listElements.emplace_back(SPopUpElement("Display","display",1,0, 130));
					bgMenu.m_origin.y += (bgMenu.m_listElements.size() * 20);
					if (bgMenu.m_origin.y > radarea.bottom) { bgMenu.m_origin.y = radarea.bottom; }
					if ((bgMenu.m_origin.x + 120) > radarea.right) { bgMenu.m_origin.x = radarea.right - 120; }

					bgMenu.drawPopUpMenu(&dc);
					for (auto& element : bgMenu.m_listElements) {
						AddScreenObject(BUTTON_MENU_RMB_MENU, element.m_function.c_str(), element.elementRect, false, element.m_text.c_str());
					}
					bgMenu.highlightSelection(&dc, menuState.MB3hoverRect);
					
				}

				// Draw the CSiT Tools Menu; starts at rad area top left then moves right
				// this point moves to the origin of each subsequent area
				POINT menutopleft = CPoint(radarea.left, radarea.top);

				TopMenu::DrawBackground(dc, menutopleft, radarea.right, 60);
				RECT but;

				// small amount of padding;
				menutopleft.y += 6;
				menutopleft.x += 6;

				if (menuLayer == 0 || menuLayer == 2) {

						POINT modOrigin;
						modOrigin.x = 8;

						string numCJSAC = to_string(menuState.numJurisdictionAC);
						TopMenu::MakeText(dc, { modOrigin.x, radarea.top + 14 }, 42, 15, numCJSAC.c_str());

						if (!menuState.setup) {

						menuButton but_tagHL = { { modOrigin.x + 50, radarea.top + 6 }, "Tag HL", 43, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_tagHL);
						ButtonToScreen(this, but, "Tag HL", 0);

						menuButton but_setup = { { modOrigin.x + 95, radarea.top + 6 }, "Setup", 43, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_setup);
						ButtonToScreen(this, but, "Open Setup", BUTTON_MENU_SETUP);

						menuButton but_aircraftState = { { modOrigin.x + 140, radarea.top + 6 }, "Aircraft State", 70, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_aircraftState);
						ButtonToScreen(this, but, "Aircraft State", 0);

						menuButton but_SSR = { { modOrigin.x + 212, radarea.top + 6 }, "SSR:", 80, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_SSR);
						ButtonToScreen(this, but, "SSR", 0);



						menuButton but_misc = { { modOrigin.x, radarea.top + 31 }, "Misc", 48, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_misc);

						menuButton but_areas = { { modOrigin.x + 50, radarea.top + 31 }, "Areas", 43, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_areas);
						ButtonToScreen(this, but, "Areas", 0);

						menuButton but_tags = { { modOrigin.x + 95, radarea.top + 31 }, "Tags", 43, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_tags);
						ButtonToScreen(this, but, "Tags", 0);


						menuButton but_flightPlan = { { modOrigin.x + 140, radarea.top + 31 }, "Flight Plan", 70, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_flightPlan);
						ButtonToScreen(this, but, "Flight Plan", 0);

						menuButton but_destAirport = { { modOrigin.x + 212, radarea.top + 31 }, "Dest Airport", 80, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_destAirport);
						ButtonToScreen(this, but, "Dest Airport", BUTON_MENU_DEST_APRT);
					}

					if (menuState.setup) {

						TopMenu::DrawBackground(dc, { 0 , radarea.top }, 305, 175);

						TopMenu::MakeText(dc, { 240, 63 }, 45, 15, "Big ACID");

						menuButton but_bigACID = { {225, 66}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.bigACID };
						but = TopMenu::DrawBut(&dc, but_bigACID);
						ButtonToScreen(this, but, "Big ACID Toggle", BUTTON_MENU_SETUP);

						// CPDLC: the Hoppie logon ICAO, and a toggle that starts and stops
						// polling. The button lights while connected.
						auto cpdlcICAO = TopMenu::MakeField(dc, { 225, 38 }, 28, 15, CPDLCMessage::hoppieICAO.c_str());
						AddScreenObject(BUTTON_MENU_CPDLC, "cpdlcICAO", cpdlcICAO, 0, "");

						menuButton but_cpdlcLogon = { {257, 36}, "Logon", 38, 20, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.CPDLCOn };
						but = TopMenu::DrawBut(&dc, but_cpdlcLogon);
						ButtonToScreen(this, but, "CPDLC Logon", BUTTON_MENU_SETUP);

						menuButton but_close_setup = { {245, 170}, "Close", 50, 20, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_close_setup);
						ButtonToScreen(this, but, "Close Setup", BUTTON_MENU_SETUP);

					}

					menutopleft.x = 310;

					if (menuLayer == 0) {

						// screen range, dummy buttons, not really necessary in ES.
						// Drawn but not registered, same as the Zoom and Pan buttons beside
						// it: on the real display Relocate re-centres the scope, which in
						// EuroScope the mouse and the ASR already do. It was registered
						// once, with the id string "Alt Filt Opts" left over from a paste,
						// and no handler was ever written - so it was a click target that
						// did nothing. Re-register it here if the behaviour is ever wanted.
						but = TopMenu::DrawButton(&dc, menutopleft, 70, 23, "Relocate", autoRefresh);
						// ButtonToScreen(this, but, "Relocate", BUTTON_MENU_RELOCATE);
						menutopleft.y += 25;

						TopMenu::DrawButton(&dc, menutopleft, 35, 23, "Zoom", 0);
						menutopleft.x += 35;
						TopMenu::DrawButton(&dc, menutopleft, 35, 23, "Pan", 0);
						menutopleft.y -= 25;
						menutopleft.x += 43;

						// horizontal range calculation
						int range = (int)round(RadRange());
						string rng = to_string(range);
						TopMenu::MakeText(dc, menutopleft, 50, 15, "Range");
						menutopleft.y += 15;

						// 109 pix per in on my monitor
						int nmIn = (int)round(109 / pixnm);
						string nmtext = "1\" = " + to_string(nmIn) + "nm";
						TopMenu::MakeText(dc, menutopleft, 50, 15, nmtext.c_str());
						menutopleft.y += 17;

						POINT downArrow[3] = {
							{-2,-2},
							{3,-2},
							{1, 3}
						};

						POINT downArrow1[3] = {
							{-2,-2},
							{3,-2},
							{1, 3}
						};

						POINT downArrow2[3] = {
							{-2,-2},
							{3,-2},
							{1, 3}
						};

						TopMenu::MakeDropDown(dc, menutopleft, 35, 15, rng.c_str());

						menuButton but_rngdropdown = { { menutopleft.x + 35, radarea.top + 38 }, "", 15, 15, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_rngdropdown);
						TopMenu::DrawIconBut(&dc, but_rngdropdown, downArrow, 3);

						menutopleft.x += 60;
						menutopleft.y -= 32;

						menutopleft.y += 8;
						TopMenu::MakeText(dc, { menutopleft.x - 8, menutopleft.y - 3 }, 35, 15, "Map");
						menutopleft.y += 18;

						TopMenu::MakeDropDown(dc, menutopleft, 75, 18, GetPlugIn()->ControllerMyself().GetSectorFileName());
						menuButton but_mapdropdown = { { menutopleft.x + 73, radarea.top + 32 }, "", 18, 18, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_mapdropdown);
						TopMenu::DrawIconBut(&dc, but_mapdropdown, downArrow1, 3);

						menutopleft.y -= 26;
						menutopleft.x += 35;
						but = TopMenu::DrawButton(&dc, menutopleft, 55, 23, "Overlays", 1);

						menutopleft.x += 70;
						menutopleft.y += 8;
						menuButton but_preset = { {menutopleft.x, radarea.top + 6 }, "Preset", 45, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_preset);

						menutopleft.y += 18;
						TopMenu::MakeDropDown(dc, menutopleft, 73, 18, "preset");
						menuButton but_presetdropdown = { { menutopleft.x + 73, radarea.top + 32 }, "", 18, 18, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_presetdropdown);
						TopMenu::DrawIconBut(&dc, but_presetdropdown, downArrow2, 3);

						menutopleft.y -= 26;
						menutopleft.x += 47;

						menuButton but_presetback = { {menutopleft.x, radarea.top + 6 }, "Back", 45, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_presetback);

						menutopleft.x += 58;

						// altitude filters

						but = TopMenu::DrawButton(&dc, menutopleft, 50, 23, "Alt Filter", altFilterOpts);
						ButtonToScreen(this, but, "Alt Filt Opts", BUTTON_MENU_ALT_FILT_OPT);

						menutopleft.y += 25;

						string altFilterLowFL = to_string(altFilterLow);
						if (altFilterLowFL.size() < 3) {
							altFilterLowFL.insert(altFilterLowFL.begin(), 3 - altFilterLowFL.size(), '0');
						}
						string altFilterHighFL = to_string(altFilterHigh);
						if (altFilterHighFL.size() < 3) {
							altFilterHighFL.insert(altFilterHighFL.begin(), 3 - altFilterHighFL.size(), '0');
						}

						string filtText = altFilterLowFL + string(" - ") + altFilterHighFL;
						but = TopMenu::DrawButton(&dc, menutopleft, 50, 23, filtText.c_str(), altFilterOn);
						ButtonToScreen(this, but, "", BUTTON_MENU_ALT_FILT_ON);
						menutopleft.y -= 25;
						menutopleft.x += 64;

						// separation tools
						string haloText = "Halo " + to_string(menuState.haloRad);
						but = TopMenu::DrawButton(&dc, menutopleft, 45, 23, haloText.c_str(), menuState.haloTool);
						ButtonToScreen(this, but, "Halo", BUTTON_MENU_HALO_TOOL);

						menutopleft.y = menutopleft.y + 25;
						string ptlText = "PTL " + to_string(menuState.ptlLength);
						but = TopMenu::DrawButton(&dc, menutopleft, 45, 23, ptlText.c_str(), CSiTRadar::menuState.ptlTool);
						ButtonToScreen(this, but, "PTL", BUTTON_MENU_PTL_TOOL);

						menutopleft.y = menutopleft.y - 25;
						menutopleft.x = menutopleft.x + 47;
						TopMenu::DrawButton(&dc, menutopleft, 35, 23, "RBL", 0);

						menutopleft.y = menutopleft.y + 25;
						TopMenu::DrawButton(&dc, menutopleft, 35, 23, "PIV", 0);

						menutopleft.y = menutopleft.y - 25;
						menutopleft.x = menutopleft.x + 37;
						TopMenu::DrawButton(&dc, menutopleft, 50, 23, "Rings 20", 0);

						menutopleft.y = menutopleft.y + 25;
						TopMenu::DrawButton(&dc, menutopleft, 50, 23, "Grid", 0);

						menutopleft.y = menutopleft.y - 25;
						menutopleft.x = menutopleft.x + 52;
						TopMenu::DrawButton(&dc, menutopleft, 50, 23, "Airspace", 0);

						// get the controller position ID and display it (aesthetics :) )
						if (GetPlugIn()->ControllerMyself().IsValid())
						{
							controllerID = GetPlugIn()->ControllerMyself().GetPositionId();
						}

						menutopleft.x += 58;
						string cid = "CJS - " + controllerID;

						RECT r = TopMenu::DrawButton2(dc, menutopleft, 55, 23, cid.c_str(), 0);

						menutopleft.y += 25;


						auto it = find_if(menuState.nearbyCJS.begin(), menuState.nearbyCJS.end(), [](const std::pair<string, bool>& c) { return c.second == true; });
						bool qckl = false;
						if (it != menuState.nearbyCJS.end()) { qckl = true; }

						but = TopMenu::DrawButton(&dc, menutopleft, 55, 23, "Qck Look", qckl);
						menutopleft.y -= 25;
						ButtonToScreen(this, but, "Qck Look", BUTTON_MENU_QUICK_LOOK);
					}

					POINT psrPoor[13] = {
						{0,0},
						{0,-5},
						{0,5},
						{0,0},
						{4,-4},
						{-4,4},
						{0,0},
						{4,4},
						{-4,-4},
						{0,0},
						{-5,0},
						{5,0},
						{0,0}
					};

					POINT targetModuleOrigin = { 980, radarea.top + 6 };
					
					if (!menuState.crda) {

						menuButton but_psrpoor = { {targetModuleOrigin.x, radarea.top + 6 }, "", 30,23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_psrpoor);
						TopMenu::DrawIconBut(&dc, but_psrpoor, psrPoor, 13);

						menuButton but_ALL = { { targetModuleOrigin.x, radarea.top + 31 }, "ALL", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.filterBypassAll };
						but = TopMenu::DrawBut(&dc, but_ALL);
						ButtonToScreen(this, but, "Ovrd Filter ALL", BUTTON_MENU_OVRD_ALL);

						menuButton but_EXT = { { targetModuleOrigin.x + 30, radarea.top + 6 }, "Ext", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.extAltToggle };
						but = TopMenu::DrawBut(&dc, but_EXT);
						ButtonToScreen(this, but, "ExtAlt Toggle", BUTTON_MENU_EXT_ALT);

						menuButton but_EMode = { { targetModuleOrigin.x + 30, radarea.top + 31 }, "EMode", 60, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_EMode);

						POINT plane[19] = {
							{0,-5},
							{-1,-4},
							{-1,-2},
							{-5,2},
							{-5,3},
							{-1,1},
							{-1,4},
							{-4,6},
							{-4,7},
							{0,6},
							{4,7},
							{4,6},
							{1,4},
							{1,1},
							{5,3},
							{5,2},
							{1,-2},
							{1,-4},
							{0,-5}
						};

						menuButton but_FPE = { { targetModuleOrigin.x + 60, radarea.top + 6 }, "", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.showExtrapFP };
						but = TopMenu::DrawBut(&dc, but_FPE);
						TopMenu::DrawIconBut(&dc, but_FPE, plane, sizeof(plane) / sizeof(plane[0]));
						ButtonToScreen(this, but, "ExtrapolatedFP", BUTTON_MENU_EXTRAP_FP);

						menuButton but_autotag = { { targetModuleOrigin.x + 92, radarea.top + 6 }, "Auto Tag", 62, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_autotag);

						menuButton but_CRDA = { { targetModuleOrigin.x + 92, radarea.top + 31 }, "CRDA", 62, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
						but = TopMenu::DrawBut(&dc, but_CRDA);
						ButtonToScreen(this, but, "CRDA", BUTTON_MENU_CRDA);

						menuButton but_ins1 = { { targetModuleOrigin.x + 162, radarea.top + 6 }, "Ins1", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_ins1);

						menuButton but_ins2 = { { targetModuleOrigin.x + 162, radarea.top + 31 }, "Ins2", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
						TopMenu::DrawBut(&dc, but_ins2);
					}

					modOrigin.x = 1180;

					menuButton but_highWx = { { modOrigin.x, radarea.top + 6 }, "High", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.wxHigh };
					but = TopMenu::DrawBut(&dc, but_highWx);
					ButtonToScreen(this, but, "WxHigh", BUTTON_MENU_WX_HIGH);

					menuButton but_allWx = { { modOrigin.x + 30, radarea.top + 6 }, "All", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.wxAll };
					but = TopMenu::DrawBut(&dc, but_allWx);
					ButtonToScreen(this, but, "WxAll", BUTTON_MENU_WX_ALL);

					menuButton but_topsWx = { { modOrigin.x, radarea.top + 31 }, "TOPS", 60, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
					TopMenu::DrawBut(&dc, but_topsWx);

					POINT icon_bolt[7] = {
						{2,-5},
						{5,-5},
						{1, 0},
						{4,0},
						{-3,5},
						{0,0},
						{-2,0}
					};

					menuButton but_lightning = { { modOrigin.x+62, radarea.top + 6 }, "", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
					TopMenu::DrawBut(&dc, but_lightning);
					TopMenu::DrawIconBut(&dc, but_lightning, icon_bolt, 7);

					menuButton but_hist = { { modOrigin.x+62, radarea.top + 31 }, "Hist", 30, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_GREY4, 0 };
					TopMenu::DrawBut(&dc, but_hist);

					menutopleft.x = menutopleft.x + 200;

				}

				// CRDA / TBS menu
				if (menuState.crda) {
				
					TopMenu::DrawBackground(dc, { 975, radarea.top }, 200, 95);

					TopMenu::MakeText(dc, { 990, 33 }, 45, 15, "TBS FAC :");

					auto crs = TopMenu::MakeField(dc, { 1040, 34 }, 32, 15, to_string(menuState.tbsHdg).c_str());
					AddScreenObject(BUTTON_MENU_TBS_HDG, "tbscrs", crs, 0, "");

					menuButton but_mixed_tbs = { {1100, 36}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.tbsMixed };
					but = TopMenu::DrawBut(&dc, but_mixed_tbs);
					ButtonToScreen(this, but, "Mixed TBS", BUTTON_MENU_TBS_MIXED);

					TopMenu::MakeText(dc, { 1115, 33 }, 35, 15, "Mixed");

					menuButton but_close_crda = { {1100, 90}, "Close", 40, 20, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_close_crda);
					ButtonToScreen(this, but, "Close Dest", BUTTON_MENU_CRDA_CLOSE);

				}


				// Destination Airport Menu

				if (menuState.destAirport) {
					TopMenu::DrawBackground(dc, { 218, radarea.top }, 330, 90);

					menuButton but_dest_1 = { {228, 36}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destArptOn[0] };
					but = TopMenu::DrawBut(&dc, but_dest_1);
					ButtonToScreen(this, but, "Dest 1", BUTTON_MENU_DEST_1);

					auto dest1 = TopMenu::MakeField(dc, { 243, 34 }, 32, 15, menuState.destICAO[0].c_str());
					AddScreenObject(BUTTON_MENU_DEST_ICAO, "dest1", dest1, 0, "");

					menuButton but_dest_2 = { {228, 62}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destArptOn[1] };
					but = TopMenu::DrawBut(&dc, but_dest_2);
					ButtonToScreen(this, but, "Dest 2", BUTTON_MENU_DEST_2);

					auto dest2 = TopMenu::MakeField(dc, { 243, 60 }, 32, 15, menuState.destICAO[1].c_str());
					AddScreenObject(BUTTON_MENU_DEST_ICAO, "dest2", dest2, 0, "");

					menuButton but_dest_3 = { {283, 36}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destArptOn[2] };
					but = TopMenu::DrawBut(&dc, but_dest_3);
					ButtonToScreen(this, but, "Dest 3", BUTTON_MENU_DEST_3);

					auto dest3 = TopMenu::MakeField(dc, { 298, 34 }, 32, 15, menuState.destICAO[2].c_str());
					AddScreenObject(BUTTON_MENU_DEST_ICAO, "dest3", dest3, 0, "");

					menuButton but_dest_4 = { {283, 62}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destArptOn[3] };
					but = TopMenu::DrawBut(&dc, but_dest_4);
					ButtonToScreen(this, but, "Dest 4", BUTTON_MENU_DEST_4);

					auto dest4 = TopMenu::MakeField(dc, { 298, 60 }, 32, 15, menuState.destICAO[3].c_str());
					AddScreenObject(BUTTON_MENU_DEST_ICAO, "dest4", dest4, 0, "");

					menuButton but_dest_5 = { {338, 83}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destArptOn[4] };
					but = TopMenu::DrawBut(&dc, but_dest_5);
					ButtonToScreen(this, but, "Dest 5", BUTTON_MENU_DEST_5);

					auto dest5 = TopMenu::MakeField(dc, { 353, 80 }, 32, 15, menuState.destICAO[4].c_str());
					AddScreenObject(BUTTON_MENU_DEST_ICAO, "dest5", dest5, 0, "");

					menuButton but_dest_dist = { {398, 36}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destDME };
					but = TopMenu::DrawBut(&dc, but_dest_dist);
					ButtonToScreen(this, but, "Dest Dist", BUTTON_MENU_DEST_DIST);
					TopMenu::MakeText(dc, { 403, 25 }, 40, 30, "DME");

					menuButton but_dest_est = { {448, 36}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destEST };
					but = TopMenu::DrawBut(&dc, but_dest_est);
					ButtonToScreen(this, but, "Dest EST", BUTTON_MENU_DEST_EST);
					TopMenu::MakeText(dc, { 451, 25 }, 40, 30, "EST");

					menuButton but_dest_vfr = { {398, 62}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, menuState.destVFR };
					but = TopMenu::DrawBut(&dc, but_dest_vfr);
					ButtonToScreen(this, but, "Dest VFR", BUTTON_MENU_DEST_VFR);
					TopMenu::MakeText(dc, { 403, 51 }, 40, 30, "VFR");


					menuButton but_close_dest_arpt = { {488, 90}, "Close", 40, 20, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_close_dest_arpt);
					ButtonToScreen(this, but, "Close Dest", BUTTON_MENU_CLOSE_DEST);
					
					menuButton but_clear_dest_arpt = { {398, 78}, "Clear All Dest", 80, 20, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_clear_dest_arpt);
					ButtonToScreen(this, but, "Clear All Dest", BUTTON_MENU_CLEAR_DEST);
				}

				// QUICK look menu

				if (menuState.quickLook) {
					TopMenu::DrawBackground(dc, { 306, radarea.top }, 670, 60);

					menuButton but_quickLook = { {900, 30}, "Close", 70, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_quickLook);
					ButtonToScreen(this, but, "Close", BUTTON_MENU_QUICK_LOOK);

					but_quickLook = { {828, 30}, "Clear All", 70, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_quickLook);
					ButtonToScreen(this, but, "Clear All", BUTTON_MENU_QUICK_LOOK);

					but_quickLook = { {828, 55}, "Select All", 70, 23, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, 0 };
					but = TopMenu::DrawBut(&dc, but_quickLook);
					ButtonToScreen(this, but, "Select All", BUTTON_MENU_QUICK_LOOK);
					
					int deltax = 0;
					int deltay = 0;

					for (auto cjs : menuState.nearbyCJS) {


						but_quickLook = { {317+deltax, radarea.top + 12 + deltay}, "", 10, 10, C_MENU_GREY3, C_MENU_GREY2, C_MENU_TEXT_WHITE, cjs.second };
						but = TopMenu::DrawBut(&dc, but_quickLook);
						ButtonToScreen(this, but, cjs.first.c_str(), BUTTON_MENU_QL_CJS);

						TopMenu::MakeTextLeft(dc, { 330 + deltax, radarea.top + 11 + deltay }, 20, 10, cjs.first.c_str());

						deltax += 38;

						if (deltax >= 440) { deltax = 0; deltay += 26; }
					}
				}

				else if (menuLayer == 1) {

				POINT elementOrigin = CPoint(radarea.left + 10, radarea.top + 6);
				RECT r;

				// Alt Filter Submenu
				if (altFilterOpts) {

					string altFilterLowFL = to_string(altFilterLow);
					if (altFilterLowFL.size() < 3) {
						altFilterLowFL.insert(altFilterLowFL.begin(), 3 - altFilterLowFL.size(), '0');
					}
					string altFilterHighFL = to_string(altFilterHigh);
					if (altFilterHighFL.size() < 3) {
						altFilterHighFL.insert(altFilterHighFL.begin(), 3 - altFilterHighFL.size(), '0');
					}


					r = TopMenu::DrawButton(&dc, elementOrigin, 50, 46, "Cancel", FALSE);
					AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "Cancel", r, 0, "");

					elementOrigin.x += 52;
					r = TopMenu::DrawButton(&dc, elementOrigin, 50, 46, "", FALSE);
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 12 }, 50, 15, "Clear");
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 22 }, 50, 15, "Filter");
					AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "Clear Filter", r, 0, "");

					elementOrigin.x += 52;
					r = TopMenu::DrawButton(&dc, elementOrigin, 50, 46, "OK", FALSE);
					AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "OK", r, 0, "");
					elementOrigin.x += 40;
					elementOrigin.y += 6;

					r = TopMenu::MakeText(dc, elementOrigin, 55, 15, "Top:");
					elementOrigin.x += 45;
					rHLim = TopMenu::MakeField(dc, elementOrigin, 25, 15, altFilterHighFL.c_str());
					AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "HLim", rHLim, 0, "");

					elementOrigin.x -= 45; elementOrigin.y += 20;

					TopMenu::MakeText(dc, elementOrigin, 55, 15, "Base:");
					elementOrigin.x += 45;
					rLLim = TopMenu::MakeField(dc, elementOrigin, 25, 15, altFilterLowFL.c_str());
					AddScreenObject(BUTTON_MENU_ALT_FILT_OPT, "LLim", rLLim, 0, "");

				}

				// Halo submenu

				if (menuState.haloTool) {

					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "Close", FALSE);
					AddScreenObject(BUTTON_MENU_HALO_CLOSE, "Close", r, 0, "");

					elementOrigin.x += 72;
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "", FALSE);
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 12 }, 70, 15, "Clear All");
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 22 }, 70, 15, "Halos");
					AddScreenObject(BUTTON_MENU_HALO_CLEAR_ALL, "Clear All Halos", r, 0, "");

					elementOrigin.x += 72;
					TopMenu::MakeText(dc, elementOrigin, 220, 13, "Halo Radius - nm");

					// Halo options loop

					elementOrigin.y += 13;
					elementOrigin.x += 20;

					
					for (int idx = 0; idx < 6; idx++) {
						bool pressed = FALSE;
						if (haloOptions[idx] == menuState.haloRad) {
							pressed = TRUE;
						}
						RECT r = TopMenu::DrawButton(&dc, elementOrigin, 20, 15, to_string(haloOptions[idx]).c_str(), pressed);
						AddScreenObject(BUTTON_MENU_HALO_OPTIONS, to_string(haloOptions[idx]).c_str(), r, 0, "");
						elementOrigin.x += 22;
					}
					

					elementOrigin.y = radarea.top + 6;
					// End PTL options

					elementOrigin.x = 370;
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "", menuState.haloCursor);
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 12 }, 70, 15, "Display");
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 22 }, 70, 15, "Halo Cursor");
					AddScreenObject(BUTTON_MENU_HALO_MOUSE, "Mouse", r, 0, "");

					elementOrigin.x = 460;
					TopMenu::MakeText(dc, elementOrigin, 200, 15, "Toggle Halo cursor ON - OFF.");
					elementOrigin.y += 16;
					TopMenu::MakeText(dc, elementOrigin, 200, 15, "Select a Halo Radius.");
					elementOrigin.y += 16;
					TopMenu::MakeText(dc, elementOrigin, 200, 15, "Click on target to toggle Halo ON - OFF.");
										
				}


				// PTL submenu
				if (menuState.ptlTool) {
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "Close", FALSE);
					AddScreenObject(BUTTON_MENU_PTL_CLOSE, "Close", r, 0, "");

					
					elementOrigin.x += 72;
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "", FALSE);
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 12 }, 70, 15, "Clear All");
					TopMenu::MakeText(dc, { elementOrigin.x, elementOrigin.y + 22 }, 70, 15, "PTLs");
					AddScreenObject(BUTTON_MENU_PTL_CLEAR_ALL, "Clear All PTLs", r, 0, "");

					elementOrigin.x += 72;
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 46, "PTL All", menuState.ptlAll);
					AddScreenObject(BUTTON_MENU_PTL_ALL_ON, "PTL All on", r, 0, "");

					elementOrigin.x += 72;
					r = TopMenu::DrawButton(&dc, elementOrigin, 70, 23, "Uncorr", FALSE);

					r = TopMenu::DrawButton(&dc, { elementOrigin.x, elementOrigin.y + 23 }, 70, 23, "Timeout", FALSE);

					elementOrigin.x += 72;
					TopMenu::MakeText(dc, elementOrigin, 250, 15, "PTL Length - Minutes");

					// PTL options loop

					elementOrigin.y += 15;
					elementOrigin.x += 12;

					for (int idx = 0; idx < 20; idx++) {
						bool pressed = FALSE;
						if (ptlOptions[idx] == menuState.ptlLength) {
							pressed = TRUE;
						}
						if (idx == 10) {
							elementOrigin.y += 15;
							elementOrigin.x -= 220;
						}
						RECT r = TopMenu::DrawButton(&dc, elementOrigin, 20, 15, to_string(ptlOptions[idx]).c_str(), pressed);
						AddScreenObject(BUTTON_MENU_PTL_OPTIONS, to_string(ptlOptions[idx]).c_str(), r, 0, "");
						elementOrigin.x += 22;
					}

					elementOrigin.y = radarea.top + 6;
					// End PTL options

					elementOrigin.x = 540;
					r = TopMenu::DrawButton(&dc, elementOrigin, 35, 46, "WB", menuState.wbPTL);
					AddScreenObject(BUTTON_MENU_PTL_WB, "WB PTL Toggle", r, 0, "");
					elementOrigin.x = 577;
					r = TopMenu::DrawButton(&dc, elementOrigin, 35, 46, "EB", menuState.ebPTL);
					AddScreenObject(BUTTON_MENU_PTL_EB, "EB PTL Toggle", r, 0, "");

					elementOrigin.x = 610;
					TopMenu::MakeText(dc, elementOrigin, 150, 15, "Click on targets to toggle");
					elementOrigin.y += 16;
					TopMenu::MakeText(dc, elementOrigin, 150, 15, "PTL ON-OFF.");
					
				}

				// Rings submenu

				}
			}
		}

		if (phase == REFRESH_PHASE_BACK_BITMAP) {
			if (menuState.wxAll || menuState.wxHigh) {

				// Direct call. As std::async with a named future this blocked here anyway,
				// so nothing was gained by the thread - but renderRadar drew into a GDI+
				// Graphics bound to this thread's HDC and made up to 65,025
				// ConvertCoordFromPositionToPixel calls into the EuroScope SDK from a
				// worker. Same timing, no cross-thread drawing or SDK use.
				wxRadar::renderRadar(&g, this, menuState.wxAll);
			}

			// refresh jurisdictional list on zoom change
			CSiTRadar::menuState.jurisdictionalAC.clear();

	
			for (auto& ac : CSiTRadar::mAcData) {

				if (ac.second.isJurisdictional) {
					if (ac.second.isOnScreen) {
						if (ac.second.isHandoff) {
							CSiTRadar::menuState.jurisdictionalAC.push_front(ac.first);
						}
						else {
							CSiTRadar::menuState.jurisdictionalAC.push_back(ac.first);
						}
					}
				}
				if (ac.second.isHandoffToMe && ac.second.isOnScreen) {
					CSiTRadar::menuState.jurisdictionalAC.push_front(ac.first);
				}
			}

			// Closed-runway markers on IFR display types: a red line along each inactive
			// runway with a 0.1nm circle at its midpoint. Disabled, together with the scan
			// in updateActiveRunways that fills inactiveRwyList. Re-enable both together.
			/*

			Pen pen(Color(255, 199, 48, 35), 1);

			if (strcmp(DisplayType.c_str(), "IFR") == 0) {
				for (auto const& rwy : menuState.inactiveRwyList) {

					POINT end1 = m_pRadScr->ConvertCoordFromPositionToPixel(rwy.end1);
					POINT end2 = m_pRadScr->ConvertCoordFromPositionToPixel(rwy.end2);

					g.SetSmoothingMode(SmoothingModeNone);

					g.DrawLine(&pen, Point(end1.x, end1.y), Point(end2.x, end2.y));

					//dc.MoveTo(end1);
					//dc.LineTo(end2);

					POINT circle;
					circle.x = (end1.x + end2.x) / 2;
					circle.y = (end1.y + end2.y) / 2;

					g.SetSmoothingMode(SmoothingModeHighQuality);

					g.DrawEllipse(&pen, circle.x - (int)round(0.1 * pixnm), circle.y - (int)round(0.1 * pixnm), (int)round(0.2 * pixnm), (int)round(0.2 * pixnm));

				}
			}
			*/
		}
	}

	

	g.ReleaseHDC(hdc);
	dc.Detach();
}

// A blank uplink addressed to an aircraft, with its identifier allocated.
//
// The real MIN range is 0 to 63; Hoppie allows more, so the counter is kept whole and
// only the value on the wire rolls over.
CPDLCMessage CSiTRadar::NewCPDLCUplink(const std::string& callsign)
{
	CPDLCMessage uplink;
	uplink.isdlMessage = false;
	uplink.messageType = "cpdlc";
	uplink.receipient = callsign;
	uplink.sender = CPDLCMessage::hoppieICAO;
	uplink.timeParsed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	uplink.messageID = (int)(CPDLCMessage::ids++ % 64);
	return uplink;
}

// Sends on a worker and records the message against the aircraft.
//
// cpdlc.cpp touches no EuroScope SDK, which is what makes moving the send off this
// thread safe. The copy is deliberate: the worker outlives this call.
void CSiTRadar::DispatchCPDLCUplink(CPDLCMessage uplink, const std::string& callsign)
{
	const std::string preview = uplink.rawMessageContent;

	std::thread send([uplink]() mutable { uplink.SendCPDLCMessage(); });
	send.detach();

	mAcData[callsign].CPDLCMessages.push_back(uplink);

	GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
		(callsign + ": " + preview).c_str(), true, true, false, false, false);

	RequestRefresh();
}

// The most recent downlink still waiting on an answer. A reply button means "answer what
// I am looking at", and this is what that is when nothing has been selected explicitly.
CPDLCMessage* CSiTRadar::LatestOpenDownlink(const std::string& callsign)
{
	auto aircraft = mAcData.find(callsign);
	if (aircraft == mAcData.end()) { return nullptr; }

	std::vector<CPDLCMessage>& messages = aircraft->second.CPDLCMessages;

	for (size_t i = messages.size(); i > 0; --i)
	{
		CPDLCMessage& candidate = messages[i - 1];
		if (!candidate.isdlMessage) { continue; }
		if (candidate.messageID == -1) { continue; }
		if (candidate.responseRequired != "Y") { continue; }

		// Answered already if anything references it.
		bool answered = false;
		for (const CPDLCMessage& other : messages)
		{
			if (other.responseToMessageID == candidate.messageID) { answered = true; break; }
		}
		if (!answered) { return &candidate; }
	}
	return nullptr;
}

// Accepts an aircraft's logon request.
//
// Hoppie's protocol for this is two plain text messages and nothing more: the aircraft
// sends REQUEST LOGON, the station answers LOGON ACCEPTED.
void CSiTRadar::AcceptCPDLCLogon(const std::string& callsign)
{
	if (mAcData[callsign].cpdlcState == CPDLC_CONNECTED) {
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
			(callsign + ": already connected").c_str(), true, true, false, false, false);
		return;
	}

	CPDLCMessage uplink = NewCPDLCUplink(callsign);
	uplink.responseRequired = "NE";
	uplink.rawMessageContent = "LOGON ACCEPTED";

	// Answer the request itself where there is one, so the two are paired in the list
	// rather than sitting there as two unrelated lines.
	CPDLCMessage* request = LatestOpenDownlink(callsign);
	if (request != nullptr && request->rawMessageContent == "REQUEST LOGON") {
		uplink.responseToMessageID = request->messageID;
	}

	mAcData[callsign].cpdlcState = CPDLC_CONNECTED;
	DispatchCPDLCUplink(uplink, callsign);
}

// Ends the CPDLC service for an aircraft.
//
// The wording comes from the [FREETEXT] entry marked UNICOM when the file has one, which
// is where a unit puts what it wants said - CZQM's reads "SERVICES TERMINATED - MNT
// UNICOM 122.8". Falling back to a plain phrase rather than refusing, because ending a
// service must not depend on a config file having the row.
void CSiTRadar::EndCPDLCService(const std::string& callsign)
{
	if (mAcData[callsign].cpdlcState != CPDLC_CONNECTED) {
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
			(callsign + ": not connected").c_str(), true, true, false, false, false);
		return;
	}

	std::string text = "CPDLC SERVICE TERMINATED";
	std::string reply = "NE";

	for (const SituCpdlcFreetext::Entry& entry : cpdlcFreetext.entries) {
		if (!entry.terminatesService) { continue; }
		text = entry.text;
		reply = entry.replyType;
		break;
	}

	CPDLCMessage uplink = NewCPDLCUplink(callsign);
	uplink.responseRequired = reply;
	uplink.rawMessageContent = text;

	mAcData[callsign].cpdlcState = CPDLC_NOT_CONNECTED;
	mAcData[callsign].cpdlcMnemonic = false;
	DispatchCPDLCUplink(uplink, callsign);
}

// Sends one of the canned messages, chosen by the button's label.
//
// The wording and the reply type both come from [FREETEXT] in SituCPDLC.txt rather than
// from a table in here, so a unit that wants "APPROVED" instead of "ROGER" edits a file.
void CSiTRadar::SendCPDLCFreetext(const std::string& callsign, const std::string& label)
{
	const SituCpdlcFreetext::Entry* entry = SituCpdlcFreetext::Find(cpdlcFreetext, label);
	if (entry == nullptr) {
		// A button with nothing behind it says so. Sending a message the controller did
		// not choose, with a reply type nobody set, is the worse outcome.
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
			(label + ": no such message in SituCPDLC.txt [FREETEXT]").c_str(),
			true, true, false, false, false);
		return;
	}

	CPDLCMessage uplink = NewCPDLCUplink(callsign);
	uplink.responseRequired = entry->replyType;
	uplink.rawMessageContent = entry->text;

	CPDLCMessage* answering = LatestOpenDownlink(callsign);
	if (answering != nullptr) { uplink.responseToMessageID = answering->messageID; }

	// The mnemonic marks an unread downlink. Answering one is what clears it.
	if (answering != nullptr) { mAcData[callsign].cpdlcMnemonic = false; }

	DispatchCPDLCUplink(uplink, callsign);
}

void CSiTRadar::CloseCPDLCEditor(const std::string& callsign)
{
	for (auto it = menuState.radarScrWindows.begin(); it != menuState.radarScrWindows.end(); ++it) {
		if (it->second.m_winType == WINDOW_CPDLC_EDITOR && it->second.m_callsign == callsign) {
			menuState.radarScrWindows.erase(it);
			return;
		}
	}
}

void CSiTRadar::OpenCPDLCEditor(const std::string& callsign, POINT at)
{
	// One editor per aircraft. A second click moves the one that is open rather than
	// stacking another on top of it.
	for (auto& win : menuState.radarScrWindows) {
		if (win.second.m_winType == WINDOW_CPDLC_EDITOR && win.second.m_callsign == callsign) {
			win.second.m_origin = at;
			return;
		}
	}

	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(callsign.c_str());
	if (!fp.IsValid()) { return; }

	// operator[] rather than at: an aircraft with no record yet would throw out of a
	// EuroScope callback, and an empty message list is the right thing to show.
	CAppWindows editor(at, WINDOW_CPDLC_EDITOR, fp, GetRadarArea(), mAcData[callsign].CPDLCMessages);
	editor.m_callsign = callsign;
	menuState.radarScrWindows[editor.m_windowId_] = editor;
}

// Composes and sends one of the manual CPDLC uplinks from the callsign menu.
//
// Everything it needs to know about the next controller comes from SituCPDLC.txt and
// from what that controller has said about themselves, rather than from a table of
// facility names compiled into the plugin.
void CSiTRadar::SendCPDLCUplink(const std::string& which)
{
	CFlightPlan fp = GetPlugIn()->FlightPlanSelectASEL();
	if (!fp.IsValid()) { return; }

	const std::string callsign = fp.GetCallsign();

	// Who the aircraft is going to. During a handoff that is the handoff target;
	// otherwise it is whoever has been coordinated as next. Both are position ids except
	// GetCoordinatedNextController, which returns a callsign, so it is converted.
	std::string nextPositionId = fp.GetHandoffTargetControllerId();
	std::string nextCallsign;

	if (!nextPositionId.empty()) {
		nextCallsign = GetPlugIn()->ControllerSelectByPositionId(nextPositionId.c_str()).GetCallsign();
	}
	else {
		nextCallsign = fp.GetCoordinatedNextController();
		if (!nextCallsign.empty()) {
			nextPositionId = GetPlugIn()->ControllerSelect(nextCallsign.c_str()).GetPositionId();
		}
	}

	if (nextCallsign.empty() && nextPositionId.empty()) {
		GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
			(callsign + ": no next controller to name; hand off or coordinate one first").c_str(),
			true, true, false, false, false);
		return;
	}

	const SituCpdlcStations::Resolution next =
		SituCpdlcStations::Resolve(cpdlcStations, nextPositionId, nextCallsign);

	CPDLCMessage uplink = NewCPDLCUplink(callsign);

	if (which == "CPDLCNDA") {
		// Refuse rather than guess. A next data authority the far end is not listening
		// on fails silently there, where the sending controller cannot see it.
		if (!next.found) {
			GetPlugIn()->DisplayUserMessage("VATCAN Situ", "CPDLC",
				(callsign + ": no CPDLC station known for " + (nextCallsign.empty() ? nextPositionId : nextCallsign)
					+ "; add it to SituCPDLC.txt").c_str(),
				true, true, false, false, false);
			return;
		}

		uplink.responseRequired = "NE";
		uplink.rawMessageContent = SituCpdlcStations::FormatHandover(cpdlcStations, next);
	}
	else if (which == "CPDLCContact" || which == "CPDLCMonitor") {
		// The radio callsign from the station table when there is one, since that is what
		// the unit is called on frequency; the raw callsign otherwise.
		const std::string spoken = next.found && !next.radioCallsign.empty()
			? next.radioCallsign
			: nextCallsign;

		const double frequency = nextCallsign.empty()
			? 0.0
			: GetPlugIn()->ControllerSelect(nextCallsign.c_str()).GetPrimaryFrequency();

		// 199.998 is what the SDK returns when no primary frequency is selected, and
		// UNICOM is where an aircraft goes when nobody is next.
		const std::string spokenFrequency = (frequency > 0.0 && frequency < 199.0)
			? CPDLCMessage::FreqTruncate(frequency)
			: std::string("122.800");

		uplink.responseRequired = "WU";
		uplink.rawMessageContent = (which == "CPDLCContact" ? "CONTACT @" : "MONITOR @")
			+ spoken + "@ @" + spokenFrequency + "@";
	}
	else {
		return;
	}

	DispatchCPDLCUplink(uplink, callsign);
}

void CSiTRadar::OnClickScreenObject(int ObjectType,
	const char* sObjectId,
	POINT Pt,
	RECT Area,
	int Button)
{

	menuState.bgM3Click = false;

	string s, id, func;
	// find window ID, then function from the string
	s = sObjectId;
	string::size_type pos = s.find(" ");
	if (pos != s.npos) {
		id = s.substr(0, pos);
		func = s.substr(pos + 1);
	}

	if (ObjectType == WINDOW_SCROLL_ARROW_UP) {
		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }
		// GetListBox now returns a pointer into m_listboxes_, so the list box can be
		// mutated in place. The old copy-clear-push_back dance existed only because it
		// returned by value.
		SListBox* lb = window->GetListBox(atoi(func.c_str()));
		if (lb == nullptr) { return; }

		lb->ScrollUp();
		lb->listBox_.clear();
		lb->PopulateDirectListBox(&mAcData[window->m_callsign].acFPRoute, GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
		RequestRefresh();
	}

	if (ObjectType == WINDOW_SCROLL_ARROW_DOWN) {
		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }
		SListBox* lb = window->GetListBox(atoi(func.c_str()));
		if (lb == nullptr) { return; }

		lb->ScrollDown();
		lb->listBox_.clear();
		lb->PopulateDirectListBox(&mAcData[window->m_callsign].acFPRoute, GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
		RequestRefresh();
	}

	if (ObjectType == TAG_ITEM_TYPE_CALLSIGN || ObjectType == TAG_ITEM_FP_CS) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId)); // make sure aircraft is ASEL

		if (Button == BUTTON_RIGHT) {
			menuState.ResetSFIOptions();
			menuState.MB3menu = true;
			menuState.MB3menuType = 0;
			menuState.MB3clickedPt = Pt;
			menuState.MB3primRect = { 0,0,0,0 };
			menuState.MB3hoverRect = { 0,0,0,0 };
			menuState.MB3SecondaryMenuOn = false;
		}
	}

	if (ObjectType == FREE_TEXT) {

		if (Button == BUTTON_LEFT) {

			int i = atoi(sObjectId);
			auto it = std::find_if(menuState.freetext.begin(), menuState.freetext.end(), [i](const SFreeText& s) {
				return s.m_id == i;
				});
			if (it != menuState.freetext.end()) {
				menuState.freetext.erase(it);
			}

		}
	}

	if (ObjectType == WINDOW_CPDLC) {
		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }

		const std::string cs = window->m_callsign;

		if (func == "Close") {
			// The editor belongs to this window; closing the parent takes it with it.
			CloseCPDLCEditor(cs);
			menuState.radarScrWindows.erase(stoi(id));
		}
		else if (func == "Close Dialog") {
			CloseCPDLCEditor(cs);
		}
		else if (func == "Connect") {
			AcceptCPDLCLogon(cs);
		}
		else if (func == "End Service") {
			EndCPDLCService(cs);
		}
		else if (func == "Flight Plan") {
			CFlightPlan fp = GetPlugIn()->FlightPlanSelect(cs.c_str());
			if (fp.IsValid()) {
				CAppWindows fpWindow(Pt, WINDOW_FLIGHT_PLAN, fp, GetRadarArea());
				fpWindow.m_callsign = cs;
				menuState.radarScrWindows[fpWindow.m_windowId_] = fpWindow;
			}
		}
		else if (!func.empty()) {
			// A canned message, looked up by the button's own label. The category
			// buttons - Altitude, Speed, Route and the rest - have no [FREETEXT] entry
			// and no value picker behind them yet, so they report that rather than
			// guessing at a message.
			SendCPDLCFreetext(cs, func);
		}

		RequestRefresh();
		return;
	}

	if (ObjectType == WINDOW_HANDOFF_EXT_CJS) {

		if (!strcmp(func.c_str(), "Cancel")) {
			menuState.radarScrWindows.erase(stoi(id));
		}
	}

	if (ObjectType == WINDOW_DIRECT_TO) {
		string c;
		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }
		string cs = window->m_callsign;

		if (!strcmp(func.c_str(), "Ok")) {


			for (const auto& lb : window->m_listboxes_) {
				for (const auto& lelem : lb.listBox_) {
					if (lelem.m_selected_) {

						c = lelem.m_ListBoxElementText;
						menuState.radarScrWindows.erase(stoi(id));
						GetPlugIn()->FlightPlanSelect(cs.c_str()).GetControllerAssignedData().SetDirectToPointName(c.c_str());

						const CPosition ppos = GetPlugIn()->RadarTargetSelect(cs.c_str()).GetPosition().GetPosition();
						string pposStr = SituPosition::FormatPositionString(ppos.m_Latitude, ppos.m_Longitude);
						pposStr += " ";
						string rtestr = GetPlugIn()->FlightPlanSelect(cs.c_str()).GetFlightPlanData().GetRoute();
						auto itr = rtestr.find(c.c_str());
						if (itr != rtestr.npos) {
							rtestr = rtestr.substr(itr);
							rtestr.insert(0, pposStr);
						}
						else {
							rtestr = pposStr;
							for (size_t i = GetPlugIn()->FlightPlanSelect(cs.c_str()).GetExtractedRoute().GetPointsAssignedIndex(); i < mAcData[cs].acFPRoute.fix_names.size(); i++) {
								rtestr += mAcData[cs].acFPRoute.fix_names.at(i) + " ";
							}
						}

						GetPlugIn()->FlightPlanSelect(cs.c_str()).GetFlightPlanData().SetRoute(rtestr.c_str());
						GetPlugIn()->FlightPlanSelect(cs.c_str()).GetFlightPlanData().AmendFlightPlan();

						mAcData[cs].directToLineOn = false;
						mAcData[cs].directToPendingPosition.m_Latitude = 0.0;
						mAcData[cs].directToPendingPosition.m_Longitude = 0.0;
						mAcData[cs].directToPendingFixName = "";
						return;
					}
					else {
						c = "";
						if (!lb.selectItem.empty()) {
							c = lb.selectItem;
						}
					}
				}
			}

			GetPlugIn()->FlightPlanSelect(cs.c_str()).GetControllerAssignedData().SetDirectToPointName(c.c_str());
			menuState.radarScrWindows.erase(stoi(id));
			mAcData[cs].directToLineOn = false;
			mAcData[cs].directToPendingPosition.m_Latitude = 0.0;
			mAcData[cs].directToPendingPosition.m_Longitude = 0.0;
			mAcData[cs].directToPendingFixName = "";

		}

		if (!strcmp(func.c_str(), "Cancel")) {
			mAcData[window->m_callsign].directToLineOn = false;
			mAcData[window->m_callsign].directToPendingPosition.m_Latitude = 0.0;
			mAcData[window->m_callsign].directToPendingPosition.m_Longitude = 0.0;
			mAcData[window->m_callsign].directToPendingFixName = "";

			menuState.radarScrWindows.erase(stoi(id));
		}
	}

	if (ObjectType == WINDOW_FREE_TEXT) {

		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }

		if (!strcmp(func.c_str(), "Submit")) {
			string c;
			// if the textbox is selected, else go with the choice in menu
			if (STextField* focused = GetFocusedTextField()) {
				c = focused->m_text;
			}
			CPosition pos;
			pos = ConvertCoordFromPixelToPosition(menuState.MB3clickedPt);
			SFreeText t;
			t.m_id = CSiTRadar::menuState.numFreeText;
			CSiTRadar::menuState.numFreeText++;
			t.m_pos = pos;
			t.m_freetext_string = c;
			menuState.freetext.push_back(t);

			menuState.radarScrWindows.erase(stoi(id));
			RefreshMapContent();
		}

		if (!strcmp(func.c_str(), "Cancel")) {
			menuState.radarScrWindows.erase(stoi(id));
		}

	}

	if (ObjectType == WINDOW_CTRL_REMARKS) {

		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }

		if (!strcmp(func.c_str(), "Blank")) {
			ModifyCtrlRemarks("", GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
			menuState.radarScrWindows.erase(stoi(id));
		}


		if (!strcmp(func.c_str(), "Cancel")) {
			menuState.radarScrWindows.erase(stoi(id));
		}

		if (!strcmp(func.c_str(), "Submit")) {
			string c; 
			// if the textbox is selected, else go with the choice in menu
			if (STextField* focused = GetFocusedTextField()) {
				c = focused->m_text;
			}
			else {
				for (const auto& lb : window->m_listboxes_) {
					for (const auto& lelem : lb.listBox_) {
						if (lelem.m_selected_) {
							c = lelem.m_ListBoxElementText;
							ModifyCtrlRemarks(c, GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
							menuState.radarScrWindows.erase(stoi(id));

							return;
						}
						else {
							c = "";
						}
					}
				}
			}
			ModifyCtrlRemarks(c, GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
			menuState.radarScrWindows.erase(stoi(id));
		}
	}

	if (ObjectType == WINDOW_POINT_OUT) {
		auto window = GetAppWindowFromObjectId(id);
		if (window == nullptr) { return; }
		if (!strcmp(func.c_str(), "Cancel")) {
			menuState.radarScrWindows.erase(stoi(id));
		}

		if (!strcmp(func.c_str(), "Submit")) {
			string poMsg = "PO " + window->m_textfields_.back().m_text;
			SendPointOut (window->m_textfields_.front().m_text.c_str(), poMsg.c_str(), &GetPlugIn()->FlightPlanSelect(window->m_callsign.c_str()));
			mAcData[window->m_callsign].pointOutFromMe = true;
			mAcData[window->m_callsign].POTarget = window->m_textfields_.front().m_text;
			mAcData[window->m_callsign].POString = window->m_textfields_.back().m_text;

			string defaultESpout;
			string callsign = window->m_callsign;
			defaultESpout = ".point " + window->m_textfields_.front().m_text;
			for (auto& c : defaultESpout) {
				c = tolower(c);
			}
			menuState.radarScrWindows.erase(stoi(id));
			ClearFocusedTextFields();

			if (!GetPlugIn()->ControllerSelectByPositionId(mAcData[callsign].POTarget.c_str()).IsOngoingAble()) { // if not ES, send a ES default p/out i.e. to US

				SituPlugin::SendKeyboardString(defaultESpout);
				GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(callsign.c_str()));
				SituPlugin::SendKeyboardPresses({ 0x4E });
			}
			else { // if ES send a text message also (if target doesn't use plugin)

				string poTarget = GetPlugIn()->ControllerSelectByPositionId(mAcData[callsign].POTarget.c_str()).GetCallsign();
				poTarget = ".chat " + poTarget;
				string poMessage = "point out " + callsign;
				poMessage = poMessage + " " + mAcData[callsign].POString;

				for (auto& c : poMessage) {
					c = std::tolower(c);
				}
				for (auto& c : poTarget) {
					c = std::tolower(c);
				}

				SituPlugin::SendKeyboardString(poTarget);
				SituPlugin::SendKeyboardPresses({ 0x1C });
				SituPlugin::SendKeyboardString(poMessage);
				SituPlugin::SendKeyboardPresses({ 0x1C });
			}

		}
	}

	if (ObjectType == WINDOW_LIST_BOX_ELEMENT) {
		string s, win, le;
		string le_text;
		s = sObjectId;
		string::size_type pos = s.find(" ");
		if (pos != s.npos) {
			win = s.substr(0, pos);
			le = s.substr(pos + 1);
		}
		auto window = GetAppWindowFromObjectId(win);
		if (window == nullptr) { return; }

		// A click in the CPDLC message list selects that message and opens the editor
		// against it, which is what a reply button then answers. The generic selection
		// below works on m_ListBoxElementText, which a CPDLC row does not use - its
		// content is the message, not that string - so it is handled here instead.
		if (window->m_winType == WINDOW_CPDLC) {
			for (auto& lb : window->m_listboxes_) {
				for (auto& lelem : lb.listBox_) {
					lelem.m_selected_ = (lelem.m_elementID == stoi(le));
				}
			}
			OpenCPDLCEditor(window->m_callsign, { Pt.x, Pt.y + 40 });
			RequestRefresh();
			return;
		}

		for (auto &lb : window->m_listboxes_) {
			for (auto &lelem : lb.listBox_) {
				if (lelem.m_elementID == stoi(le)) {
					lelem.m_selected_ = true;
					le_text = lelem.m_ListBoxElementText;
					lb.selectItem = le_text;
				}
				else {
					lelem.m_selected_ = false;
				}
			}
		}

		if (window->m_winType == WINDOW_DIRECT_TO) {
			mAcData[window->m_callsign].directToLineOn = true;
			if (!mAcData[window->m_callsign].acFPRoute.fix_names.empty() && !mAcData[window->m_callsign].acFPRoute.route_fix_positions.empty()) {
				std::vector<string>::iterator it = std::find(mAcData[window->m_callsign].acFPRoute.fix_names.begin(), mAcData[window->m_callsign].acFPRoute.fix_names.end(), le_text);
				if (it != mAcData[window->m_callsign].acFPRoute.fix_names.end()) {
					int index = std::distance(mAcData[window->m_callsign].acFPRoute.fix_names.begin(), it);
					mAcData[window->m_callsign].directToPendingPosition = mAcData[window->m_callsign].acFPRoute.route_fix_positions.at(index);
					mAcData[window->m_callsign].directToPendingFixName = le_text;
				}
				else {
					mAcData[window->m_callsign].directToPendingPosition.m_Latitude = 0.0;
					mAcData[window->m_callsign].directToPendingPosition.m_Longitude = 0.0;
					mAcData[window->m_callsign].directToPendingFixName = "";
					mAcData[window->m_callsign].directToLineOn = false;
				}
			}
		}

	}

	if (ObjectType == WINDOW_TEXT_FIELD) {
		string s, win, tf;
		s = sObjectId;
		string::size_type pos = s.find(" ");
		if (pos != s.npos) {
			win = s.substr(0, pos);
			tf = s.substr(pos + 1);
		}

		// Only one text field can be focused in whole app
		for (auto& window : menuState.radarScrWindows) {
			for (auto& textf : window.second.m_textfields_) {
				if (window.first == atoi(win.c_str())) {
					if (textf.m_textFieldID == atoi(tf.c_str())) {
						textf.m_focused = !textf.m_focused;
						if (textf.m_focused) {
							SetFocusedTextField(window.first, textf.m_textFieldID);
						}
						else {
							menuState.focusedItem.m_focus_on = false;
						}
					}
					else {
						textf.m_focused = false;
					}
				}
				else {
					textf.m_focused = false;
				}
			}
		}
	}

}




CAppWindows* CSiTRadar::GetAppWindow(int winID) {
	if (menuState.radarScrWindows.count(winID) != 0) {
		return &menuState.radarScrWindows.at(winID);
	}
	return nullptr;
}

void CSiTRadar::OnButtonDownScreenObject(int ObjectType,
	const char* sObjectId,
	POINT Pt,
	RECT Area,
	int Button)
{	

	if (menuState.mouseMMB) { return; }

	if (ObjectType == HIGHLIGHT_POINT_OUT_ACCEPT) {
		CSiTRadar::mAcData[sObjectId].pointOutPendingApproval = false;
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		string poTarget = GetPlugIn()->ControllerSelectByPositionId(mAcData[sObjectId].POTarget.c_str()).GetCallsign();
		for (auto& c : poTarget) {
			c = std::tolower(c);
		}
		poTarget = ".chat " + poTarget;
		string poMessage = sObjectId;
		for (auto& c : poMessage) {
			c = std::tolower(c);
		}
		poMessage += " ok";
		SituPlugin::SendKeyboardString(poTarget);
		SituPlugin::SendKeyboardPresses({ 0x1C });
		SituPlugin::SendKeyboardString(poMessage);
		SituPlugin::SendKeyboardPresses({ 0x1C });
	}

	if (ObjectType == BUTTON_MENU_RMB_MENU) {
		if (!strcmp(sObjectId, "AutoHandoff")) {
			GetPlugIn()->FlightPlanSelectASEL().InitiateHandoff(GetPlugIn()->FlightPlanSelectASEL().GetCoordinatedNextController());
			menuState.MB3menu = false;
		}
		if (!strcmp(sObjectId, "FltPlan")) {
			menuState.MB3menu = false;
			StartTagFunction(GetPlugIn()->FlightPlanSelectASEL().GetCallsign(), NULL, TAG_ITEM_TYPE_PLANE_TYPE, sObjectId, NULL, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, Pt, Area);
		}
		if (!strcmp(sObjectId, "AssumeTrack")) {
			menuState.MB3menu = false;
			GetPlugIn()->FlightPlanSelectASEL().StartTracking();
		}
		if (!strcmp(sObjectId, "DropTrack")) {
			menuState.MB3menu = false;
			GetPlugIn()->FlightPlanSelectASEL().EndTracking();
		}
		if (!strcmp(sObjectId, "Decorrelate")) {
			menuState.MB3menu = false;
			GetPlugIn()->FlightPlanSelectASEL().Uncorrelate();
		}		

		if (!strcmp(sObjectId, "DirectTo")) {
			menuState.MB3menu = false;

			// maintain route in plugin ac-model
			int numPts = GetPlugIn()->FlightPlanSelectASEL().GetExtractedRoute().GetPointsNumber();
			string cs = GetPlugIn()->FlightPlanSelectASEL().GetCallsign();
			ACRoute rte;
			rte.fix_names.clear();
			rte.route_fix_positions.clear();
			mAcData[cs].acFPRoute = rte;
			for (int i = 0; i < numPts; i++) {
				rte.fix_names.push_back(GetPlugIn()->FlightPlanSelectASEL().GetExtractedRoute().GetPointName(i));
				rte.route_fix_positions.push_back(GetPlugIn()->FlightPlanSelectASEL().GetExtractedRoute().GetPointPosition(i));
			}
			mAcData[cs].acFPRoute = rte;

			// Check if there is already a window, bring it to the mouse
			bool exists = false;
			for (auto& win : menuState.radarScrWindows) {
				if (!strcmp(win.second.m_callsign.c_str(), GetPlugIn()->FlightPlanSelectASEL().GetCallsign())
					&& win.second.m_winType == WINDOW_DIRECT_TO)
				{
					win.second.m_origin = { Pt.x, Pt.y };
					exists = true;
				}
			}
			// If not draw it
			if (!exists) {
				CAppWindows dctto({ Pt.x, Pt.y }, WINDOW_DIRECT_TO, GetPlugIn()->FlightPlanSelectASEL(), GetRadarArea(), &mAcData[GetPlugIn()->FlightPlanSelectASEL().GetCallsign()].acFPRoute);
				menuState.radarScrWindows[dctto.m_windowId_] = dctto;
			}
		}

		if (!strcmp(sObjectId, "CtrlRemarks")) {
			menuState.MB3menu = false;
			// Check if there is already a window, bring it to the mouse
			bool exists = false;
			for (auto& win : menuState.radarScrWindows) {
				if (!strcmp(win.second.m_callsign.c_str(), GetPlugIn()->FlightPlanSelectASEL().GetCallsign())
					&& win.second.m_winType == WINDOW_CTRL_REMARKS)
				{
					win.second.m_origin = { Pt.x, Pt.y };
					exists = true;
				}
			}
			// If not draw it
			if (!exists) {
				CAppWindows ctrl({ Pt.x, Pt.y}, WINDOW_CTRL_REMARKS, GetPlugIn()->FlightPlanSelectASEL(), GetRadarArea(), &menuState.ctrlRemarkDefaults);
				menuState.radarScrWindows[ctrl.m_windowId_] = ctrl;
			}
		}

		if (!strcmp(sObjectId, "freetext")) {
			menuState.MB3menu = false;
			menuState.bgM3Click = false;
			ClearFocusedTextFields();

			CAppWindows freetxt({ Pt.x, Pt.y }, WINDOW_FREE_TEXT, GetRadarArea());
			freetxt.m_textfields_.front().m_focused = true; // bring focus to text field for keyboard, only one text field in free text dialogue

			menuState.radarScrWindows[freetxt.m_windowId_] = freetxt;

		}


		if (!strcmp(sObjectId, "clrfreetext")) {
			menuState.MB3menu = false;
			menuState.bgM3Click = false;

			menuState.freetext.clear();
		}

		if (!strcmp(sObjectId, "RecallPointOut")) {
			menuState.MB3menu = false;
			mAcData[GetPlugIn()->FlightPlanSelectASEL().GetCallsign()].pointOutFromMe = false;
			GetPlugIn()->FlightPlanSelectASEL().GetControllerAssignedData().SetFlightStripAnnotation(1, "");
			SendPointOut(mAcData[GetPlugIn()->FlightPlanSelectASEL().GetCallsign()].POTarget.c_str(), "", &GetPlugIn()->FlightPlanSelect(GetPlugIn()->FlightPlanSelectASEL().GetCallsign()));

			mAcData[GetPlugIn()->FlightPlanSelectASEL().GetCallsign()].POTarget = "";
			mAcData[GetPlugIn()->FlightPlanSelectASEL().GetCallsign()].POString = "";

		}
		if (!strcmp(sObjectId, "AcceptPointOut")) {
			menuState.MB3menu = false;

			// In this branch sObjectId is the menu item's function name, not a callsign.
			// This block was copy-pasted from the HIGHLIGHT_POINT_OUT_ACCEPT handler,
			// where sObjectId really is a callsign, and never adapted: it keyed mAcData
			// on "AcceptPointOut", looked up a flight plan of that name, and sent the
			// other controller a private message reading "acceptpointout ok".
			string callsign = GetPlugIn()->FlightPlanSelectASEL().GetCallsign();
			if (callsign.empty()) { return; }

			CSiTRadar::mAcData[callsign].pointOutPendingApproval = false;
			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(callsign.c_str()));
			string poTarget = GetPlugIn()->ControllerSelectByPositionId(mAcData[callsign].POTarget.c_str()).GetCallsign();
			for (auto& c : poTarget) {
				c = std::tolower(c);
			}
			poTarget = ".chat " + poTarget;
			string poMessage = callsign;
			for (auto& c : poMessage) {
				c = std::tolower(c);
			}
			poMessage += " ok";
			SituPlugin::SendKeyboardString(poTarget);
			SituPlugin::SendKeyboardPresses({ 0x1C });
			SituPlugin::SendKeyboardString(poMessage);
			SituPlugin::SendKeyboardPresses({ 0x1C });

		}
	}

	if (ObjectType == BUTTON_MENU_RMB_MENU_SECONDARY) {
		if (!strcmp(menuState.MB3SecondaryMenuType.c_str(), "ManHandoff")) {
			menuState.MB3menu = false;

			// "EXP" opens the manual CJS entry dialog; it is not a position id. The
			// InitiateHandoff below used to run first regardless, so choosing EXP tried
			// to hand the aircraft to a position literally called "EXP" before opening
			// the dialog.
			if (!strcmp(sObjectId, "EXP")) {
				ClearFocusedTextFields();
				CAppWindows extCJS(Pt, WINDOW_HANDOFF_EXT_CJS, GetPlugIn()->FlightPlanSelectASEL(), GetRadarArea());
				menuState.radarScrWindows[extCJS.m_windowId_] = extCJS;
				return;
			}

			GetPlugIn()->FlightPlanSelectASEL().InitiateHandoff(GetPlugIn()->ControllerSelectByPositionId(sObjectId).GetCallsign());
		}
		if (!strcmp(menuState.MB3SecondaryMenuType.c_str(), "ModSFI")) {
			ModifySFI(sObjectId, GetPlugIn()->FlightPlanSelectASEL());
			if (strcmp(sObjectId, "EXP")) {
				menuState.MB3menu = false;
			}
		}
		if (!strcmp(menuState.MB3SecondaryMenuType.c_str(), "CPDLCMenu")) {
			menuState.MB3menu = false;
			SendCPDLCUplink(sObjectId);
		}
		if (!strcmp(menuState.MB3SecondaryMenuType.c_str(), "SetComm")) {
			menuState.MB3menu = false;
			GetPlugIn()->FlightPlanSelectASEL().GetControllerAssignedData().SetCommunicationType(*sObjectId);
		}
		if (!strcmp(menuState.MB3SecondaryMenuType.c_str(), "PointOut")) {
			menuState.MB3menu = false;
			// Check if there is already a window, bring it to the mouse
			bool exists = false;
			for (auto& win : menuState.radarScrWindows) {
				if (!strcmp(win.second.m_callsign.c_str(), GetPlugIn()->FlightPlanSelectASEL().GetCallsign())
					&& win.second.m_winType == WINDOW_POINT_OUT)
				{
					win.second.m_origin = { Pt.x, Pt.y };
					exists = true;
				}
			}
			// If not draw it
			if (!exists) {
				CAppWindows poPrompt({ Pt.x, Pt.y }, WINDOW_POINT_OUT, GetPlugIn()->FlightPlanSelectASEL(), GetRadarArea());

				ClearFocusedTextFields();

				if (!strcmp(sObjectId, "EXP")) {
					poPrompt.m_textfields_.front().m_focused = true;
				}
				else {
					poPrompt.m_textfields_.back().m_focused = true;
					poPrompt.m_textfields_.front().m_text = sObjectId;
				}

				menuState.radarScrWindows[poPrompt.m_windowId_] = poPrompt;
			}
		}
	}

	if (ObjectType == AIRCRAFT_SYMBOL) {
		
		CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
		string callsign = rt.GetCallsign();

		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));

		if (Button == BUTTON_LEFT) {
			if (!menuState.haloTool && !menuState.ptlTool) {
				if (CSiTRadar::mAcData[sObjectId].tagType == 0) { CSiTRadar::mAcData[sObjectId].tagType = 1; }

				else if (CSiTRadar::mAcData[sObjectId].tagType == 1 &&
					!GetPlugIn()->FlightPlanSelect(sObjectId).GetTrackingControllerIsMe() &&
					!menuState.filterBypassAll &&
					!CSiTRadar::mAcData[sObjectId].isQuickLooked ) {
					CSiTRadar::mAcData[sObjectId].tagType = 0;
				} // can't make bravo tag if you are tracking or if quick look is on
			}
			
			if (menuState.haloTool == TRUE) {

				if (hashalo.find(callsign) != hashalo.end()) {
					hashalo.erase(callsign);
				}
				else {
					hashalo[callsign] = TRUE;
				}
			}

			if (menuState.ptlTool == TRUE) {

				if (hasPTL.find(callsign) != hasPTL.end()) {
					hasPTL.erase(callsign);
				}
				else {
					hasPTL[callsign] = TRUE;
				}
			}
		}
	}

	if (ObjectType == BUTON_MENU_DEST_APRT) {
		menuState.destAirport = true;
		menuState.quickLook = false;
		menuLayer = 0;
	}

	if (ObjectType == BUTTON_MENU_SETUP) {
		if (!strcmp(sObjectId, "Open Setup")) {
			menuState.setup = true;
		}
		if (!strcmp(sObjectId, "Big ACID Toggle")) {
			menuState.bigACID = !menuState.bigACID;
		}
		if (!strcmp(sObjectId, "CPDLC Logon")) {
			menuState.CPDLCOn = !menuState.CPDLCOn;

			if (menuState.CPDLCOn) {
				// Poll straight away rather than waiting up to a minute for the timer.
				StartCPDLCPoll();
				menuState.lastCPDLCPoll = clock();
			}
			else {
				// Re-arm the peek for the next logon. Upstream sets firstPeek false after
				// the first fetch and never sets it back, so a logoff and logon in the same
				// session went straight to poll - which consumes messages, and skips the
				// leading Hoppie message id that parseDLMessage only reads in peek mode.
				CPDLCMessage::firstPeek = true;
			}
		}

		if (!strcmp(sObjectId, "Close Setup")) {
			menuState.setup = false;
		}
	}

	if (ObjectType == BUTTON_MENU_CPDLC) {
		if (!strcmp(sObjectId, "cpdlcICAO")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_CPDLC_ICAO, CPDLCMessage::hoppieICAO.c_str());
		}
	}

	if (ObjectType == BUTTON_MENU_CRDA) {
		menuState.crda = true;
		menuState.quickLook = false;
		menuLayer = 0;
	}

	if (ObjectType == BUTTON_MENU_CRDA_CLOSE) {
		menuState.crda = false;
	}

	if (ObjectType == BUTTON_MENU_TBS_MIXED)
	{
		menuState.tbsMixed = !menuState.tbsMixed;
	}

	if (ObjectType == BUTTON_MENU_TBS_HDG) {
		GetPlugIn()->OpenPopupEdit(Area, FUNCTION_TBS_HDG, to_string(menuState.tbsHdg).c_str());
	}

	if (ObjectType == TBS_FOLLOWER_TOGGLE) {
		mAcData[sObjectId].follower++;
		if (mAcData[sObjectId].follower > 3) {
			mAcData[sObjectId].follower = 0;
		}
	}

	if (ObjectType == BUTTON_MENU_CLOSE_DEST) {
		menuState.destAirport = false;
	}

	if (ObjectType >= BUTTON_MENU_DEST_1 && ObjectType <= BUTTON_MENU_DEST_5) {
		menuState.destArptOn[ObjectType - BUTTON_MENU_DEST_1] = !menuState.destArptOn[ObjectType - BUTTON_MENU_DEST_1];
	}

	if (ObjectType == BUTTON_MENU_DEST_ICAO) {
		if (!strcmp(sObjectId, "dest1")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_DEST_ICAO_1, menuState.destICAO[0].c_str());
		}
		else if (!strcmp(sObjectId, "dest2")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_DEST_ICAO_2, menuState.destICAO[1].c_str());
		}
		else if (!strcmp(sObjectId, "dest3")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_DEST_ICAO_3, menuState.destICAO[2].c_str());
		}
		else if (!strcmp(sObjectId, "dest4")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_DEST_ICAO_4, menuState.destICAO[3].c_str());
		}
		else if (!strcmp(sObjectId, "dest5")) {
			GetPlugIn()->OpenPopupEdit(Area, FUNCTION_DEST_ICAO_5, menuState.destICAO[4].c_str());
		}
	}

	if (ObjectType == BUTTON_MENU_CLEAR_DEST) {
		for (auto& it : menuState.destArptOn) {
			it = false;
		}
	}
	
	if (ObjectType == BUTTON_MENU_HALO_TOOL) {
		menuState.haloTool = true;
		menuLayer = 1;
	}

	if (ObjectType == BUTTON_MENU_DEST_DIST) {
		menuState.destDME = !menuState.destDME;
	}
	if (ObjectType == BUTTON_MENU_DEST_VFR) {
		menuState.destVFR = !menuState.destVFR;
	}
	if (ObjectType == BUTTON_MENU_DEST_EST) {
		menuState.destEST = !menuState.destEST;
	}

	if (ObjectType == BUTTON_MENU_HALO_OPTIONS) {
		menuState.haloRad = stoi(sObjectId);
	}

	if (ObjectType == BUTTON_MENU_HALO_CLOSE) {
		menuState.haloTool = false;
		menuLayer = 0;
	}

	if (ObjectType == BUTTON_MENU_HALO_CLEAR_ALL) {
		hashalo.clear();
	}

	if (ObjectType == BUTTON_MENU_HALO_MOUSE) {
		menuState.haloCursor = !menuState.haloCursor;
	}

	if (ObjectType == BUTTON_MENU_PTL_OPTIONS) {
		menuState.ptlLength = stoi(sObjectId);
	}

	if (ObjectType == BUTTON_MENU_PTL_WB) {
		menuState.wbPTL = !menuState.wbPTL;
	}

	if (ObjectType == BUTTON_MENU_PTL_EB) {
		menuState.ebPTL = !menuState.ebPTL;
	}


	if (ObjectType == BUTTON_MENU_PTL_TOOL) {
		menuState.ptlTool = true;
		menuLayer = 1;
	}

	if (ObjectType == BUTTON_MENU_PTL_CLOSE) {
		menuState.ptlTool = false;
		menuLayer = 0;
	}

	if (ObjectType == BUTTON_MENU_PTL_CLEAR_ALL) { 
		hasPTL.clear();
		menuState.ptlAll = false;
	}

	if (ObjectType == BUTTON_MENU_PTL_ALL_ON) { menuState.ptlAll = !menuState.ptlAll; }

	if (ObjectType == BUTTON_MENU_EXTRAP_FP) {
		menuState.showExtrapFP = !menuState.showExtrapFP;
	}

	if (ObjectType == BUTTON_MENU_OVRD_ALL) {

		if (menuState.filterBypassAll == FALSE) {
			menuState.filterBypassAll = TRUE;

			for (auto& p : CSiTRadar::mAcData) {
				CSiTRadar::tempTagData[p.first] = p.second.tagType;
				// Do not open uncorrelated tags
				if (p.second.tagType == 0) {
					p.second.tagType = 1;
				}
			}

		}
		else if (menuState.filterBypassAll == TRUE) {

			for (auto& p : CSiTRadar::tempTagData) {
				// prevents closing of tags that became under your jurisdiction during quicklook
				if (!GetPlugIn()->FlightPlanSelect(p.first.c_str()).GetTrackingControllerIsMe()) {
					CSiTRadar::mAcData[p.first].tagType = p.second;
				}
			}

			tempTagData.clear();
			menuState.filterBypassAll = FALSE;
		}
	}

	if (ObjectType == BUTTON_MENU_EXT_ALT) {
		menuState.extAltToggle = !menuState.extAltToggle;
	}
	

	if (ObjectType == BUTTON_MENU_ALT_FILT_OPT) {
		if (!strcmp(sObjectId, "Alt Filt Opts")) { 
			altFilterOpts = !altFilterOpts;
			menuLayer = 1;
		}

		if (!strcmp(sObjectId, "Cancel")) {
			altFilterOpts = false;
			menuLayer = 0;
		}

		if (!strcmp(sObjectId, "LLim")) {
			string altFilterLowFL = to_string(altFilterLow);
			if (altFilterLowFL.size() < 3) {
				altFilterLowFL.insert(altFilterLowFL.begin(), 3 - altFilterLowFL.size(), '0');
			}
			GetPlugIn()->OpenPopupEdit(rLLim, FUNCTION_ALT_FILT_LOW, altFilterLowFL.c_str());
		}
		if (!strcmp(sObjectId, "HLim")) {
			string altFilterHighFL = to_string(altFilterHigh);
			if (altFilterHighFL.size() < 3) {
				altFilterHighFL.insert(altFilterHighFL.begin(), 3 - altFilterHighFL.size(), '0');
			}
			GetPlugIn()->OpenPopupEdit(rHLim, FUNCTION_ALT_FILT_HIGH, altFilterHighFL.c_str());
		}
		if (!strcmp(sObjectId, "OK")) {
			string s = to_string(altFilterHigh);
			SaveDataToAsr("altFilterHigh", "Alt Filter High Limit", s.c_str());
			s = to_string(altFilterLow);
			SaveDataToAsr("altFilterLow", "Alt Filter Low Limit", s.c_str());
			altFilterOpts = false;
			menuLayer = 0;
		}
	}

	if (ObjectType == BUTTON_MENU_ALT_FILT_ON) {
		altFilterOn = !altFilterOn;
	}

	if (ObjectType == BUTTON_MENU_QUICK_LOOK) {
		if (!strcmp(sObjectId, "Qck Look")) {
			menuState.quickLook = true;
			menuState.destAirport = false;
			menuLayer = 2;
		}
		
		if (!strcmp(sObjectId, "Close")) {
			menuState.quickLook = false;
			menuLayer = 0;
		}

		if (!strcmp(sObjectId, "Clear All")) {
			for (auto &cjs : menuState.nearbyCJS) {
				cjs.second = false;
			}
		}

		if (!strcmp(sObjectId, "Select All")) {
			for (auto &cjs : menuState.nearbyCJS) {
				cjs.second = true;
			}
		}

	}

	if (ObjectType == BUTTON_MENU_QL_CJS) {
		if (menuState.nearbyCJS.find(sObjectId) != menuState.nearbyCJS.end()) {
			menuState.nearbyCJS[sObjectId] = !menuState.nearbyCJS[sObjectId];
		}
		RequestRefresh();
	}

	if (ObjectType == BUTTON_MENU_WX_HIGH) {
		if (menuState.wxAll) { menuState.wxAll = false; }
		
		menuState.wxHigh = !menuState.wxHigh;
		RefreshMapContent();
		
		if (menuState.lastWxRefresh == 0 || (clock() - menuState.lastWxRefresh) / CLOCKS_PER_SEC > 600) {
			
			// Direct call - the future's destructor blocked this click handler anyway.
			wxRadar::parseRadarPNG(this);
			menuState.lastWxRefresh = clock();
		}
	}

	if (ObjectType == BUTTON_MENU_WX_ALL) {
		if (menuState.wxHigh) { menuState.wxHigh = false; }
		
		menuState.wxAll = !menuState.wxAll;
		RefreshMapContent();

		if (menuState.lastWxRefresh == 0 || (clock() - menuState.lastWxRefresh) / CLOCKS_PER_SEC > 600) {
			// Direct call - the future's destructor blocked this click handler anyway.
			wxRadar::parseRadarPNG(this);
			menuState.lastWxRefresh = clock();
		}
	}
	
	if (ObjectType == TAG_ITEM_TYPE_CALLSIGN || ObjectType == TAG_ITEM_FP_CS) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId)); // make sure aircraft is ASEL
		
		if (Button == BUTTON_LEFT) {
			if (mAcData[sObjectId].isHandoffToMe == TRUE) {
				GetPlugIn()->FlightPlanSelect(sObjectId).AcceptHandoff();
			}
			else {
				StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_PLANE_TYPE, sObjectId, NULL, TAG_ITEM_FUNCTION_TOGGLE_ROUTE_DRAW, Pt, Area);
			}
		}

		if (Button == BUTTON_RIGHT) {
			//CPopUpMenu acPopup(Pt, &GetPlugIn()->FlightPlanSelect(sObjectId), m_pRadScr);
			//acPopup.drawPopUpMenu(CSiTRadar::m_dcPtr);
			//StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_CALLSIGN, sObjectId, NULL, TAG_ITEM_FUNCTION_HANDOFF_POPUP_MENU, Pt, Area);
		}
	}

	if (ObjectType == TAG_ITEM_TYPE_SQUAWK) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->RadarTargetSelect(sObjectId));
		if (Button == BUTTON_RIGHT) {
			
			StartTagFunction(GetPlugIn()->RadarTargetSelect(sObjectId).GetSystemID(), NULL, TAG_ITEM_TYPE_SQUAWK, GetPlugIn()->RadarTargetSelect(sObjectId).GetSystemID(), NULL, TAG_ITEM_FUNCTION_CORRELATE_POPUP, Pt, Area);
		}
	}

	if (ObjectType == TAG_ITEM_TYPE_ALTITUDE) {
		if (Button == BUTTON_RIGHT) {		
			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_ALTITUDE, sObjectId, NULL, TAG_ITEM_FUNCTION_TEMP_ALTITUDE_POPUP, Pt, Area);
		}

		if (Button == BUTTON_LEFT) {
			if (menuState.extAltToggle) {
				mAcData[sObjectId].extAlt = !mAcData[sObjectId].extAlt;
			}
		}
	}

	if (ObjectType == TAG_ITEM_TYPE_GROUND_SPEED_WITH_N) {
		if (Button == BUTTON_LEFT) {
			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_GROUND_SPEED_WITH_N, sObjectId, NULL, TAG_ITEM_FUNCTION_ASSIGNED_SPEED_POPUP, Pt, Area);
		}

		if (Button == BUTTON_RIGHT) {
			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_GROUND_SPEED_WITH_N, sObjectId, NULL, TAG_ITEM_FUNCTION_ASSIGNED_MACH_POPUP, Pt, Area);
		}
	}

	if (ObjectType == TAG_ITEM_TYPE_PLANE_TYPE) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		if (Button == BUTTON_LEFT) {
			
		}
		if (Button == BUTTON_RIGHT) {

			GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId)); // make sure aircraft is ASEL
			
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_PLANE_TYPE, sObjectId, NULL, TAG_ITEM_FUNCTION_OPEN_FP_DIALOG, Pt, Area);
			
		}
	}

	if (ObjectType == CTR_DATA_TYPE_SCRATCH_PAD_STRING || ObjectType == TAG_ITEM_TYPE_COMMUNICATION_TYPE) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		if (Button == BUTTON_LEFT) {
			StartTagFunction(sObjectId, NULL, CTR_DATA_TYPE_SCRATCH_PAD_STRING, sObjectId, NULL, TAG_ITEM_FUNCTION_EDIT_SCRATCH_PAD, Pt, Area);
		}
		if (Button == BUTTON_RIGHT) {
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_COMMUNICATION_TYPE, sObjectId, NULL, TAG_ITEM_FUNCTION_COMMUNICATION_POPUP, Pt, Area);
		}
	}

	if (ObjectType == TAG_ITEM_TYPE_DESTINATION) {
		GetPlugIn()->SetASELAircraft(GetPlugIn()->FlightPlanSelect(sObjectId));
		if (Button == BUTTON_RIGHT) {
			StartTagFunction(sObjectId, NULL, TAG_ITEM_TYPE_DESTINATION, sObjectId, NULL, TAG_ITEM_FUNCTION_ASSIGNED_HEADING_POPUP, Pt, Area);
		}

		auto itr = std::find(begin(CSiTRadar::menuState.destICAO), end(CSiTRadar::menuState.destICAO), GetPlugIn()->FlightPlanSelect(sObjectId).GetFlightPlanData().GetDestination());
		bool isDest = false;


		if (itr != end(CSiTRadar::menuState.destICAO)
			&& strcmp(GetPlugIn()->FlightPlanSelect(sObjectId).GetFlightPlanData().GetDestination(), "") != 0) {
			if (CSiTRadar::menuState.destArptOn[distance(CSiTRadar::menuState.destICAO, itr)]) {
				isDest = true;
			}
		}

		if (!isDest) {
			if (Button == BUTTON_LEFT) {
				if (mAcData[sObjectId].destLabelType < 3) {
					mAcData[sObjectId].destLabelType += 1;
				}
				else {
					mAcData[sObjectId].destLabelType = 0;
				}
			}
		}
	}

	if (ObjectType == LIST_TIME_ATIS) {
		acLists[LIST_TIME_ATIS].collapsed = !acLists[LIST_TIME_ATIS].collapsed;
	}
	if (ObjectType == LIST_OFF_SCREEN) {
		acLists[LIST_OFF_SCREEN].collapsed = !acLists[LIST_OFF_SCREEN].collapsed;
	}
	if (ObjectType == LIST_MESSAGES) {
		acLists[LIST_MESSAGES].collapsed = !acLists[LIST_MESSAGES].collapsed;
	}
}

void CSiTRadar::OnOverScreenObject(int ObjectType,
	const char* sObjectId,
	POINT Pt,
	RECT Area) {

	if (ObjectType == BUTTON_MENU_RMB_MENU) {

		if (!EqualRect(&Area, &CPopUpMenu::prevRect)) {

			CopyRect(&menuState.MB3hoverRect, &Area);
			CopyRect(&menuState.MB3primRect, &Area);
			menuState.MB3hoverOn = true;

			menuState.MB3SecondaryMenuOn = false;
			menuState.MB3SecondaryMenuType = sObjectId;

			if (!strcmp(sObjectId, "ManHandoff")  ||
				!strcmp(sObjectId, "ModSFI") ||
				!strcmp(sObjectId, "SetComm") ||
				!strcmp(sObjectId, "PointOut")) {
				menuState.MB3SecondaryMenuOn = true;
				menuState.MB3SecondaryMenuType = sObjectId;
			}

			CSiTRadar::m_pRadScr->RequestRefresh();
		}
	}

	if (ObjectType == BUTTON_MENU_RMB_MENU_SECONDARY) {
		if (!EqualRect(&Area, &CPopUpMenu::prevRect)) {

			CopyRect(&menuState.MB3hoverRect, &Area);
			menuState.MB3hoverOn = true;
			CSiTRadar::m_pRadScr->RequestRefresh();
		}
	}
}

void CSiTRadar::OnMoveScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, bool Released) {
	
	// Handling moving of the tags rendered by the plugin
	CRadarTarget rt = GetPlugIn()->RadarTargetSelect(sObjectId);
	CFlightPlan fp = GetPlugIn()->FlightPlanSelect(sObjectId);

	POINT p{ 0,0 };

	if (menuState.mouseMMB) {

		if (ObjectType == FREE_TEXT) {

			int i = atoi(sObjectId);
			auto it = std::find_if(menuState.freetext.begin(), menuState.freetext.end(), [i](const SFreeText& s) {
				return s.m_id == i;
				});
			// The result was dereferenced without checking for end(). A drag on a free
			// text item that had just been cleared wrote through the end iterator.
			if (it != menuState.freetext.end()) {
				it->m_pos = ConvertCoordFromPixelToPosition({ Pt.x - ((Area.right - Area.left) / 2), Pt.y - ((Area.bottom - Area.top) / 2) });
			}

		}

		if (ObjectType == TAG_ITEM_FP_CS) {

			if (fp.IsValid()) {
				p = ConvertCoordFromPositionToPixel(fp.GetFPTrackPosition().GetPosition());
			}

			RECT temp = Area;

			POINT q;
			q.x = ((temp.right + temp.left) / 2) - p.x - (TAG_WIDTH / 2); // Get centre of box 
			q.y = ((temp.top + temp.bottom) / 2) - p.y - (TAG_HEIGHT / 2);	 //(small nudge of a few pixels for error correcting with IRL behaviour) 

			// check maximal offset
			if (q.x > TAG_MAX_X_OFFSET) { q.x = TAG_MAX_X_OFFSET; }
			if (q.x < -TAG_MAX_X_OFFSET - CSiTRadar::mAcData[rt.GetCallsign()].tagWidth) { q.x = -TAG_MAX_X_OFFSET - TAG_WIDTH; }
			if (q.y > TAG_MAX_Y_OFFSET) { q.y = TAG_MAX_Y_OFFSET; }
			if (q.y < -TAG_MAX_Y_OFFSET - TAG_HEIGHT) { q.y = -TAG_MAX_Y_OFFSET - TAG_HEIGHT; }

			// nudge tag if necessary (near horizontal, or if directly above target)
			if (q.x > -((TAG_WIDTH) / 2) && q.x < 3) { q.x = 3; };
			if (q.x > -TAG_WIDTH && q.x <= -(TAG_WIDTH / 2)) { q.x = -TAG_WIDTH; }
			if (q.y > -14 && q.y < 0) { q.y = -7; }; //sticky horizon

			fptagOffset[sObjectId] = q;

			if (!Released) {

			}
			else {

			}
		}

		if (ObjectType == TAG_ITEM_TYPE_CALLSIGN ||
			ObjectType == TAG_ITEM_TYPE_ALTITUDE ||
			ObjectType == TAG_ITEM_TYPE_GROUND_SPEED_WITH_N ||
			ObjectType == TAG_ITEM_TYPE_PLANE_TYPE ||
			ObjectType == TAG_ITEM_TYPE_DESTINATION
			) {


			if (fp.IsValid()) {
				p = ConvertCoordFromPositionToPixel(rt.GetPosition().GetPosition());
			}

			RECT temp = Area;

			POINT q;
			q.x = ((temp.right + temp.left) / 2) - p.x - (CSiTRadar::mAcData[rt.GetCallsign()].tagWidth / 2); // Get centre of box 
			q.y = ((temp.top + temp.bottom) / 2) - p.y - (TAG_HEIGHT / 2);	 //(small nudge of a few pixels for error correcting with IRL behaviour) 

			// check maximal offset
			if (q.x > TAG_MAX_X_OFFSET) { q.x = TAG_MAX_X_OFFSET; }
			if (q.x < -TAG_MAX_X_OFFSET - (CSiTRadar::mAcData[rt.GetCallsign()].tagWidth)) { q.x = -TAG_MAX_X_OFFSET - CSiTRadar::mAcData[rt.GetCallsign()].tagWidth; }
			if (q.y > TAG_MAX_Y_OFFSET) { q.y = TAG_MAX_Y_OFFSET; }
			if (q.y < -TAG_MAX_Y_OFFSET - TAG_HEIGHT) { q.y = -TAG_MAX_Y_OFFSET - TAG_HEIGHT; }

			// nudge tag if necessary (near horizontal, or if directly above target)
			if (q.x > -((CSiTRadar::mAcData[rt.GetCallsign()].tagWidth) / 2) && q.x < 3) { q.x = 3; };
			if (q.x > -CSiTRadar::mAcData[rt.GetCallsign()].tagWidth && q.x <= -(CSiTRadar::mAcData[rt.GetCallsign()].tagWidth / 2)) { q.x = -CSiTRadar::mAcData[rt.GetCallsign()].tagWidth; }
			if (q.y > -14 && q.y < 0) { q.y = -7; }; //sticky horizon

			rtagOffset[sObjectId] = q;

			if (!Released) {

			}
			else {

				if (menuState.filterBypassAll) { tempTagData[sObjectId] = 1; } // if you move a tag during quick look, it will stay open
				// once released, check that the tag does not exceed the limits, and then save it to the map
			}


			RequestRefresh();
		}
		
		// The drag gives an absolute screen point; what is stored is its offset from the
		// radar area origin, so the list keeps its place relative to the header strip.
		if (ObjectType == LIST_TIME_ATIS || ObjectType == LIST_OFF_SCREEN || ObjectType == LIST_MESSAGES) {
			const RECT listArea = GetRadarArea();
			ACList& list = acLists[ObjectType];
			list.offset = { Pt.x - ((Area.right - Area.left) / 2) - listArea.left,
			                Pt.y - ((Area.bottom - Area.top) / 2) - listArea.top };
			list.offsetIsAbsolute = false;
			ClampListOffset(list, listArea);
		}

		if (ObjectType == WINDOW_TITLE_BAR) {
			if (menuState.radarScrWindows.count(stoi(sObjectId)) != 0) {
				menuState.radarScrWindows.at(stoi(sObjectId)).m_origin 
					= { Area.left , Area.top};
			}
		}
	}
}

void CSiTRadar::OnFunctionCall(int FunctionId,
	const char* sItemString,
	POINT Pt,
	RECT Area) {

	if (FunctionId == FUNCTION_ALT_FILT_LOW) {
		try {
			altFilterLow = stoi(sItemString);
		}
		catch (...) {}
	}
	if (FunctionId == FUNCTION_ALT_FILT_HIGH) {
		try {
			altFilterHigh = stoi(sItemString);
		}
		catch (...) {}
	}

	if (FunctionId == FUNCTION_CPDLC_ICAO) {
		string ICAO = sItemString;
		std::transform(ICAO.begin(), ICAO.end(), ICAO.begin(), ::toupper);
		CPDLCMessage::hoppieICAO = ICAO.substr(0, 4);
	}

	if (FunctionId >= FUNCTION_DEST_ICAO_1 && FunctionId <= FUNCTION_DEST_ICAO_5) {
		string ICAO = sItemString;
		std::transform(ICAO.begin(), ICAO.end(), ICAO.begin(), ::toupper);
		menuState.destICAO[FunctionId - FUNCTION_DEST_ICAO_1] = ICAO.substr(0,4).c_str();
	}
	if (FunctionId == FUNCTION_RMB_POPUP) {
		// Draw the right click popup menu

	}
	if (FunctionId == FUNCTION_TBS_HDG) {
		try {
			menuState.tbsHdg = stoi(sItemString);
		}
		catch (...) {}
	}
}

void CSiTRadar::ButtonToScreen(CSiTRadar* radscr, const RECT& rect, const string& btext, int itemtype) {
	AddScreenObject(itemtype, btext.c_str(), rect, 0, "");
}

void CSiTRadar::updateActiveRunways(int i) {
	vector<string> activerwys{};

	m_pRadScr->GetPlugIn()->SelectActiveSectorfile();
	CSiTRadar::menuState.activeArpt.clear();
	menuState.activeRunwaysList.clear();
	menuState.activeRunways.clear();
	menuState.activeArrivalRunways.clear();
	// menuState.inactiveRwyList.clear();   // see the disabled scan below
	// Active runway highlighting for ground screens
	for (CSectorElement runway = m_pRadScr->GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); runway.IsValid();
		runway = m_pRadScr->GetPlugIn()->SectorFileElementSelectNext(runway, SECTOR_ELEMENT_RUNWAY)) {
		string airport;

		if (runway.IsElementActive(true, 0) || runway.IsElementActive(true, 1) || runway.IsElementActive(false, 0) || runway.IsElementActive(false, 1)) {

			airport = runway.GetAirportName();
			CSiTRadar::menuState.activeArpt.insert(airport.substr(0, 4));

			string airportrwy = airport + runway.GetRunwayName(0);
			activerwys.push_back(airportrwy);

			// Record each end that is active for arrivals, with the course an aircraft
			// landing on it flies. The course is the bearing from this threshold to the
			// far one, which is a true bearing by construction - no magnetic variation
			// is involved anywhere, which is what let the hardcoded 10 for Toronto go.
			for (int end = 0; end <= 1; end++) {
				if (!runway.IsElementActive(false, end)) { continue; }

				CPosition here, there;
				runway.GetPosition(&here, end);
				runway.GetPosition(&there, 1 - end);

				ActiveArrivalRunway arrival;
				arrival.airport = airport.substr(0, 4);
				arrival.name = runway.GetRunwayName(end);
				arrival.trueCourse = HaloTool::bearingBetween(here, there);

				// "05" is 050 magnetic, "23" is 230. Runway designators carry the
				// magnetic course by definition, so a typed course can be matched
				// against them without converting either side.
				arrival.magneticDesignator = atoi(arrival.name.c_str()) * 10;

				menuState.activeArrivalRunways.push_back(arrival);
			}
		}
	}

	// Collects the runways at an in-use airport that are themselves not in use, for the
	// red closed-runway markers drawn on IFR display types. Disabled together with that
	// drawing block in OnRefresh: the markers were never enabled, so this was a second
	// full pass over every runway in the sector file building a list nothing read.
	// Re-enable both together, or neither.
	/*
	for (CSectorElement runway = m_pRadScr->GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RUNWAY); runway.IsValid();
		runway = m_pRadScr->GetPlugIn()->SectorFileElementSelectNext(runway, SECTOR_ELEMENT_RUNWAY)) {
		string airportrwy;

		for (auto const& arpt : menuState.activeArpt) {
			
			string r = runway.GetAirportName();

			if (!strcmp(r.substr(0, 4).c_str(), arpt.c_str())) {
				if (runway.IsElementActive(true, 0) || runway.IsElementActive(true, 1) || runway.IsElementActive(false, 0) || runway.IsElementActive(false, 1)) {}
				else {
					inactiveRunway r;
					CPosition p;
					runway.GetPosition(&p, 0);
					r.end1 = p;
					runway.GetPosition(&p, 1);
					r.end2 = p;

					menuState.inactiveRwyList.push_back(r);
				}
			}
		}
	}
	*/

	

	for (CSectorElement sectorElement = CSiTRadar::m_pRadScr->GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_GEO); sectorElement.IsValid();
		sectorElement = CSiTRadar::m_pRadScr->GetPlugIn()->SectorFileElementSelectNext(sectorElement, SECTOR_ELEMENT_GEO)) {

		string name = sectorElement.GetName();

		if (name.find("ACTIVE") != string::npos) {
			menuState.activeRunwaysList.push_back(sectorElement);
			for (const auto& rwy : activerwys) {
				if (name.find(rwy) != string::npos) {
					menuState.activeRunways.push_back(sectorElement);
				}
			}
		}
	}
}

void CSiTRadar::DisplayActiveRunways() {

	for (auto rwy : menuState.activeRunwaysList) {
		m_pRadScr->ShowSectorFileElement(rwy, rwy.GetComponentName(0), false);
	}

	for (auto rwy : menuState.activeRunways) {
		if (CSiTRadar::m_pRadScr->GetDataFromAsr("DisplayTypeName") != NULL) {
			string DisplayType = CSiTRadar::m_pRadScr->GetDataFromAsr("DisplayTypeName");
			// Only toggle highlighting on non-radar screens
			if (strcmp(DisplayType.c_str(), "VFR") == 0 ||
				strcmp(DisplayType.c_str(), "IFR") == 0) {
				continue;
			}
		}
		m_pRadScr->ShowSectorFileElement(rwy, rwy.GetComponentName(0), true);
	}

	m_pRadScr->RefreshMapContent();
}

void CSiTRadar::OnAsrContentLoaded(bool Loaded) {
	const char* filt = nullptr;

	// A sector file may have been reloaded under the same name, which the cache's
	// name-based staleness check cannot see. Rebuilding here costs one scan per ASR load
	// rather than one per aircraft per frame.
	CACTag::InvalidateAirportCache();

	// getting altitude filter information
	if ((filt = GetDataFromAsr("altFilterHigh")) != NULL) {
		altFilterHigh = atoi(filt);
	}
	if ((filt = GetDataFromAsr("altFilterLow")) != NULL) {
		altFilterLow = atoi(filt);
	} 

	if (menuState.activeRunwaysList.empty()) {
		// Run on the main thread. As a detached thread this walked the sector file through
		// the EuroScope SDK and wrote menuState.activeArpt, activeRunwaysList,
		// activeRunways and inactiveRwyList with no lock, while OnRefresh read activeArpt
		// from DrawACList - concurrent access to a std::set is undefined behaviour.
		// OnAirportRunwayActivityChanged already calls this synchronously, so the two
		// paths now agree. The cost is two sector file walks during ASR load.
		CSiTRadar::updateActiveRunways(0);
	}

	// DisplayActiveRunways();


	const char* sfi;
	if ((sfi = GetDataFromAsr("prefSFI")) != NULL) {
		menuState.SFIPrefStringASRSetting = sfi;
	}
	else {
		menuState.SFIPrefStringASRSetting = menuState.SFIPrefStringDefault;
	}

	// Find the position of ADSB radars
	/*
	for (CSectorElement sectorElement = GetPlugIn()->SectorFileElementSelectFirst(SECTOR_ELEMENT_RADARS); sectorElement.IsValid();
		sectorElement = GetPlugIn()->SectorFileElementSelectNext(sectorElement, SECTOR_ELEMENT_RADARS))	{
		if (!strcmp(sectorElement.GetName(), "QER")) {
			sectorElement.GetPosition(&adsbSite, 0);
		}
	}
	*/
	// Initialization of squawk code menustate

	for (CFlightPlan flightPlan = GetPlugIn()->FlightPlanSelectFirst(); flightPlan.IsValid();
		flightPlan = GetPlugIn()->FlightPlanSelectNext(flightPlan)) {
		auto itr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&flightPlan](SSquawkCodeManagement& m)->bool {return !strcmp(m.fpcs.c_str(), flightPlan.GetCallsign()); });
		if (itr == menuState.squawkCodes.end()) {
			SSquawkCodeManagement sq;
			sq.fpcs = flightPlan.GetCallsign();
			sq.squawk = flightPlan.GetControllerAssignedData().GetSquawk();
			sq.numCorrelatedRT = 0;
			menuState.squawkCodes.push_back(sq);
		}
		else {
			menuState.squawkCodes.at(distance(menuState.squawkCodes.begin(), itr)).squawk = flightPlan.GetControllerAssignedData().GetSquawk();
		}

	}
	//
} 

void CSiTRadar::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan)
{

	int count = 0;
	CSiTRadar::menuState.jurisdictionalAC.clear();

	for (auto& ac : CSiTRadar::mAcData) {

		if (ac.second.isJurisdictional) {
			count++;
			if (ac.second.isOnScreen) {
				if (ac.second.isHandoff) {
					CSiTRadar::menuState.jurisdictionalAC.push_front(ac.first);
				}
				else {
					CSiTRadar::menuState.jurisdictionalAC.push_back(ac.first);
				}
			}
		}
		if (ac.second.isHandoffToMe && ac.second.isOnScreen) {
			CSiTRadar::menuState.jurisdictionalAC.push_front(ac.first);
		}
	}

	menuState.numJurisdictionAC = count;

	// These items don't need to be updated each loop, save loop type by storing data in a map
	string callSign = FlightPlan.GetCallsign();

	bool isVFR = !strcmp(FlightPlan.GetFlightPlanData().GetPlanType(), "V");

	string icaoACData = FlightPlan.GetFlightPlanData().GetAircraftInfo(); // logic to 
	// Get information about the Aircraft/Flightplan
	// check against map

	// Read both capability maps once under a shared lock. These are republished wholesale
	// by the VATSIM datafeed worker, so an unsynchronised lookup could land mid-rehash.
	// Using find() also drops the throw-and-swallow pattern that .at() required.
	bool isRVSM{ false };
	bool isADSBFromFeed{ false };
	{
		std::shared_lock<std::shared_mutex> capabilityLock(CSiTRadar::acCapabilityMutex);

		auto rvsmEntry = CSiTRadar::acRVSM.find(callSign);
		if (rvsmEntry != CSiTRadar::acRVSM.end()) { isRVSM = rvsmEntry->second; }

		auto adsbEntry = CSiTRadar::acADSB.find(callSign);
		if (adsbEntry != CSiTRadar::acADSB.end()) { isADSBFromFeed = adsbEntry->second; }
	}

	// first check for ICAO; then check FAA
	if (FlightPlan.GetFlightPlanData().GetCapibilities() == 'L' ||
		FlightPlan.GetFlightPlanData().GetCapibilities() == 'W' ||
		FlightPlan.GetFlightPlanData().GetCapibilities() == 'Z') {
		isRVSM = TRUE;
	}

	bool isADSB = isADSBFromFeed;

	string remarks = FlightPlan.GetFlightPlanData().GetRemarks();
	
	string CJS = FlightPlan.GetTrackingControllerId();
	string origin = FlightPlan.GetFlightPlanData().GetOrigin();
	string destin = FlightPlan.GetFlightPlanData().GetDestination();

	// Update in place, touching only the fields this callback actually owns.
	//
	// This used to build a fresh ACData and assign it over the whole record. EuroScope
	// fires this callback on ANY flight plan amendment - route, cruise altitude, aircraft
	// type, remarks - so that assignment silently discarded everything the plugin owns and
	// EuroScope knows nothing about: the history-dot trail, an in-progress point-out
	// (pointOutToMe / POTarget / POString / pointOutPendingApproval), a pending direct-to,
	// the extended-altitude and destination-label toggles, the TBS follower category, the
	// handoff flags, and the open/closed tag state. Only acFPRoute was hand-copied back.
	//
	// A new callsign still gets a value-initialised (zeroed) ACData from operator[].
	ACData& acdata = mAcData[callSign];

	if (FlightPlan.GetTrackingControllerIsMe()) {
		acdata.tagType = 1;
	}
	acdata.hasVFRFP = isVFR;
	acdata.isRVSM = isRVSM;

	// Recomputed from the current flight plan every call, never latched - assigning rather
	// than only ever setting true preserves the previous semantics now that the record
	// carries over between calls.
	acdata.isADSB = isADSB || (remarks.find("STS/ADSB") != remarks.npos);
	acdata.isMedevac = (remarks.find("STS/MEDEVAC") != remarks.npos);

	/*
	if (!origin.empty() && !destin.empty()) {
		if (origin.at(0) == 'K' || destin.at(0) == 'K' || origin.at(0) == 'P' || destin.at(0) == 'P') { acdata.isADSB = true; }
	}
	// assume mediums and up have ADSB
	if (FlightPlan.GetFlightPlanData().GetAircraftWtc() == 'M' ||
		FlightPlan.GetFlightPlanData().GetAircraftWtc() == 'H' ||
		FlightPlan.GetFlightPlanData().GetAircraftWtc() == 'J')
	{
		acdata.isADSB = true;
	}
	*/
}

void CSiTRadar::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan,
	int DataType) {

	// update the menustate.squawkcodes only if the planes data gets changed

	if (DataType == CTR_DATA_TYPE_SQUAWK) {

		auto itr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&FlightPlan](SSquawkCodeManagement& m)->bool {return !strcmp(m.fpcs.c_str(), FlightPlan.GetCallsign()); });
		if (itr == menuState.squawkCodes.end()) {
			SSquawkCodeManagement sq;
			sq.fpcs = FlightPlan.GetCallsign();
			sq.squawk = FlightPlan.GetControllerAssignedData().GetSquawk();
			sq.numCorrelatedRT = 0;
			menuState.squawkCodes.push_back(sq);
		}
		else {
			menuState.squawkCodes.at(distance(menuState.squawkCodes.begin(), itr)).squawk = FlightPlan.GetControllerAssignedData().GetSquawk();
		}

	}
		/*
		// if not tracked, or tracked by me, then do a dupe squawk check
		if (!strcmp(FlightPlan.GetTrackingControllerId(), "") ||
			FlightPlan.GetTrackingControllerIsMe()) {

			auto sitr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&FlightPlan](SSquawkCodeManagement& m)->bool {return !strcmp(m.squawk.c_str(), FlightPlan.GetControllerAssignedData().GetSquawk()); });
			if (sitr != menuState.squawkCodes.end()) {
				if (!strcmp(sitr->squawk.c_str(), FlightPlan.GetControllerAssignedData().GetSquawk())) {
					if (strcmp(sitr->fpcs.c_str(), FlightPlan.GetCallsign())) {
						GetPlugIn()->DisplayUserMessage("VATCAN Situ", "Squawk Assignment Warning", ("Squawk code " + sitr->squawk + " already assigned to " + sitr->fpcs).c_str(), true, true, false, false, false);
					}
				}
			}

			auto itr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&FlightPlan](SSquawkCodeManagement& m)->bool {return !strcmp(m.fpcs.c_str(), FlightPlan.GetCallsign()); });
			if (itr == menuState.squawkCodes.end()) {
				SSquawkCodeManagement sq;
				sq.fpcs = FlightPlan.GetCallsign();
				sq.squawk = FlightPlan.GetControllerAssignedData().GetSquawk();
				sq.numCorrelatedRT = 0;
				menuState.squawkCodes.push_back(sq);
			}
			else {
				// if asigned squawk is updated
				itr->squawk = FlightPlan.GetControllerAssignedData().GetSquawk();
				itr->numCorrelatedRT = 0;
			}

		}
	}
	*/
}

void CSiTRadar::OnFlightPlanDisconnect(CFlightPlan FlightPlan) {
	string callSign = FlightPlan.GetCallsign();

	// Ten maps are keyed by callsign; only mAcData was cleaned here. The rest grew for the
	// lifetime of the session, and the consequence was not just memory: an aircraft that
	// disconnected and reconnected got its halo, its PTL and its dragged tag position back
	// without the controller asking for them.
	mAcData.erase(callSign);
	tempTagData.erase(callSign);
	hoAcceptedTime.erase(callSign);

	hashalo.erase(callSign);
	hasPTL.erase(callSign);
	isBlinking.erase(callSign);
	isHandOffHold.erase(callSign);
	ppsCJS.erase(callSign);
	rtagOffset.erase(callSign);
	fptagOffset.erase(callSign);
	connectorOrigin.erase(callSign);
}

// Where a list is drawn: its stored offset applied to the radar area origin, which is the
// same anchor the header strip uses (CPoint(radarea.left, radarea.top)). Move or resize the
// radar area and the lists travel with the header rather than staying put in screen space.
POINT CSiTRadar::ListOrigin(const ACList& list, const RECT& radarea)
{
	return { radarea.left + list.offset.x, radarea.top + list.offset.y };
}

// Keeps enough of a list inside the radar area to grab hold of. Without this a position
// saved on a wide monitor leaves the list off the edge of a narrower one, unreachable
// except by editing settings.json.
void CSiTRadar::ClampListOffset(ACList& list, const RECT& radarea)
{
	const int width = radarea.right - radarea.left;
	const int height = radarea.bottom - radarea.top;

	// Nothing sensible to clamp against before the screen has a size.
	if (width <= 0 || height <= 0) { return; }

	// The header strip occupies the top 60 pixels, so a list cannot be dropped underneath it.
	const int minY = 60;
	const int maxX = width - LIST_MIN_VISIBLE_WIDTH;
	const int maxY = height - LIST_MIN_VISIBLE_HEIGHT;

	if (list.offset.x < 0) { list.offset.x = 0; }
	if (list.offset.y < minY) { list.offset.y = minY; }
	if (maxX > 0 && list.offset.x > maxX) { list.offset.x = maxX; }
	if (maxY > minY && list.offset.y > maxY) { list.offset.y = maxY; }
}

// Converts positions read from a settings file that predates anchored offsets, then clamps
// every list. The conversion subtracts the radar area origin the value was implicitly
// measured against, so a list stays where its owner last put it.
void CSiTRadar::ResolveListOffsets(const RECT& radarea)
{
	for (auto& entry : acLists) {
		ACList& list = entry.second;
		if (list.offsetIsAbsolute) {
			list.offset.x -= radarea.left;
			list.offset.y -= radarea.top;
			list.offsetIsAbsolute = false;
		}
		ClampListOffset(list, radarea);
	}
}

void CSiTRadar::DrawACList(POINT p, CDC* dc, unordered_map<string, ACData>& ac, int listType)
{	
	int sDC = dc->SaveDC();

	dc->SetTextColor(C_WHITE);
	dc->SelectObject(CFontHelper::Euroscope14);
	string header;

	// Draw the heading
	RECT listHeading{};
	listHeading.left = p.x;
	listHeading.top = p.y;

	// Draw the arrow
	HPEN targetPen = CreatePen(PS_SOLID, 1, C_WHITE);;
	HBRUSH targetBrush = CreateSolidBrush(C_WHITE);

	dc->SelectObject(targetPen);
	dc->SelectObject(targetBrush);
	
	bool collapsed{ false };
	bool showArrow = false;

	if (listType == LIST_TIME_ATIS) {
		struct tm gmt;
		time_t t = std::time(0);
		gmtime_s(&gmt, &t);

		char timeStr[50];
		strftime(timeStr, 50, "%y-%m-%d %R", &gmt);

		dc->DrawText(timeStr, &listHeading, DT_LEFT | DT_CALCRECT);
		dc->DrawText(timeStr, &listHeading, DT_LEFT);

		AddScreenObject(LIST_TIME_ATIS, to_string(LIST_TIME_ATIS).c_str(), listHeading, true, "");

		// 1st aircraft of a list
		RECT listArpt{};
		listArpt.left = p.x;
		listArpt.top = p.y + 13;

		std::shared_lock<shared_mutex> atisLock(wxRadar::atisLetterMutex, defer_lock);
		std::shared_lock<shared_mutex> altimLock(wxRadar::altimeterMutex, defer_lock);
	
		if (atisLock.try_lock()) {
			if (wxRadar::arptAtisLetter != menuState.arptAtisLetterOld) {
				menuState.arptAtisLetterOld = wxRadar::arptAtisLetter;
			}
			atisLock.unlock();
		}

		if (altimLock.try_lock()) {
			if (wxRadar::arptAltimeter != menuState.arptAltimeterOld) {
				menuState.arptAltimeterOld = wxRadar::arptAltimeter;
			}
			altimLock.unlock();
		}

		if (altimLock.try_lock()) {
			for (auto& arpt : CSiTRadar::menuState.activeArpt) {
				string arptString = arpt.substr(1, 3);

				if (wxRadar::arptAltimeter.find(arpt.c_str()) != wxRadar::arptAltimeter.end()) {
					arptString += " - " + wxRadar::arptAltimeter.at(arpt.c_str());
				}
				else {
					arptString += " - ****";
				}

				if (atisLock.try_lock()) {
					if (wxRadar::arptAtisLetter.find(arpt.c_str()) != wxRadar::arptAtisLetter.end()) {
						arptString += " - " + wxRadar::arptAtisLetter.at(arpt.c_str());
					}
					atisLock.unlock();
				}
				else { // Get the cached version of the ATIS letter
					if (menuState.arptAtisLetterOld.find(arpt.c_str()) != menuState.arptAtisLetterOld.end()) {
						arptString += " - " + menuState.arptAtisLetterOld.at(arpt.c_str());
					}
				}

				if (!acLists[LIST_TIME_ATIS].collapsed) {
					dc->DrawText(arptString.c_str(), &listArpt, DT_LEFT | DT_CALCRECT);
					dc->DrawText(arptString.c_str(), &listArpt, DT_LEFT);
					listArpt.top += 13;
				}
				showArrow = true;
			}
			altimLock.unlock();
		}
		else {
			for (auto& arpt : CSiTRadar::menuState.activeArpt) {
				string arptString = arpt.substr(1, 3);

				if (menuState.arptAltimeterOld.find(arpt.c_str()) != menuState.arptAltimeterOld.end()) {
					arptString += " - " + menuState.arptAltimeterOld.at(arpt.c_str());
				}
				else {
					arptString += " - ****";
				}

				if (atisLock.try_lock()) {
					if (wxRadar::arptAtisLetter.find(arpt.c_str()) != wxRadar::arptAtisLetter.end()) {
						arptString += " - " + wxRadar::arptAtisLetter.at(arpt.c_str());
					}
					atisLock.unlock();
				}
				else { // Get the cached version of the ATIS letter
					if (menuState.arptAtisLetterOld.find(arpt.c_str()) != menuState.arptAtisLetterOld.end()) {
						arptString += " - " + menuState.arptAtisLetterOld.at(arpt.c_str());
					}
				}

				if (!acLists[LIST_TIME_ATIS].collapsed) {
					dc->DrawText(arptString.c_str(), &listArpt, DT_LEFT | DT_CALCRECT);
					dc->DrawText(arptString.c_str(), &listArpt, DT_LEFT);
					listArpt.top += 13;
				}
				showArrow = true;
			}
		}
		

		if (showArrow) {
			POINT vertices[] = { {listHeading.right + 5, listHeading.top + 3}, {listHeading.right + 15, listHeading.top + 3} ,  {listHeading.right + 10, listHeading.top + 10} };
			if (!acLists[LIST_TIME_ATIS].collapsed)
			{
				vertices[0] = { listHeading.right + 5, listHeading.top + 10 };
				vertices[1] = { listHeading.right + 15, listHeading.top + 10 };
				vertices[2] = { listHeading.right + 10, listHeading.top + 3 };
			}
			dc->Polygon(vertices, 3);
		}
	}
	
	if (listType == LIST_OFF_SCREEN) {

		// 1st aircraft of a list
		RECT listArcft{};
		listArcft.left = p.x + 10;
		listArcft.top = p.y + 13;

		header = "Off Screen";

		dc->DrawText(header.c_str(), &listHeading, DT_LEFT | DT_CALCRECT);
		dc->DrawText(header.c_str(), &listHeading, DT_LEFT);
		AddScreenObject(LIST_OFF_SCREEN, to_string(LIST_OFF_SCREEN).c_str(), listHeading, true, "");

		// Add the aircrafts

		for (auto &aircraft : ac) {
			if (aircraft.second.isJurisdictional && !aircraft.second.isOnScreen) {
				if (!acLists[LIST_OFF_SCREEN].collapsed) {
					dc->DrawText(aircraft.first.c_str(), &listArcft, DT_LEFT | DT_CALCRECT);
					dc->DrawText(aircraft.first.c_str(), &listArcft, DT_LEFT);
					listArcft.top += 13;
				}
				showArrow = true;
			}
		}

		if (showArrow) {
			POINT vertices[] = { {listHeading.right + 5, listHeading.top + 3}, {listHeading.right + 15, listHeading.top + 3} ,  {listHeading.right + 10, listHeading.top + 10} };
			if (!acLists[LIST_OFF_SCREEN].collapsed)
			{
				vertices[0] = { listHeading.right + 5, listHeading.top + 10 };
				vertices[1] = { listHeading.right + 15, listHeading.top + 10 };
				vertices[2] = { listHeading.right + 10, listHeading.top + 3 };
			}
			dc->Polygon(vertices, 3);
		}
	}

	if (listType == LIST_MESSAGES) {

		// Derived from mAcData every frame, the same way the off-screen list is, rather
		// than accumulated into a store. The correlation logic re-evaluates
		// multipleDiscrete on every pass, so a message that is no longer true simply stops
		// being drawn - no expiry, no stale entries, nothing to clear by hand.
		//
		// Codes are collected into a set first because several targets can be squawking
		// the same duplicated code, and the controller only needs to be told once.
		set<string> offendingCodes;

		for (auto& aircraft : ac) {
			if (!aircraft.second.multipleDiscrete) { continue; }

			CRadarTarget rt = GetPlugIn()->RadarTargetSelect(aircraft.first.c_str());
			if (!rt.IsValid()) { continue; }

			offendingCodes.insert(rt.GetPosition().GetSquawk());
		}

		// An alert list with nothing to say is clutter, and a permanently visible empty
		// one trains you to stop looking at it. Draw nothing at all when quiet.
		if (offendingCodes.empty()) {
			dc->RestoreDC(sDC);
			DeleteObject(targetPen);
			DeleteObject(targetBrush);
			return;
		}

		header = "Message List (" + to_string(offendingCodes.size()) + ")";

		dc->DrawText(header.c_str(), &listHeading, DT_LEFT | DT_CALCRECT);
		dc->DrawText(header.c_str(), &listHeading, DT_LEFT);
		AddScreenObject(LIST_MESSAGES, to_string(LIST_MESSAGES).c_str(), listHeading, true, "");

		RECT listMsg{};
		listMsg.left = p.x + 10;
		listMsg.top = p.y + 13;

		if (!acLists[LIST_MESSAGES].collapsed) {
			// General condition first, then one line naming each duplicated code.
			const string summary = "NO CORRELATION MULTI DISCRETE";
			dc->DrawText(summary.c_str(), &listMsg, DT_LEFT | DT_CALCRECT);
			dc->DrawText(summary.c_str(), &listMsg, DT_LEFT);
			listMsg.top += 13;

			for (const auto& code : offendingCodes) {
				const string line = "MULT DSCRT UNASSOC " + code;
				dc->DrawText(line.c_str(), &listMsg, DT_LEFT | DT_CALCRECT);
				dc->DrawText(line.c_str(), &listMsg, DT_LEFT);
				listMsg.top += 13;
			}
		}

		POINT vertices[] = { {listHeading.right + 5, listHeading.top + 3}, {listHeading.right + 15, listHeading.top + 3} ,  {listHeading.right + 10, listHeading.top + 10} };
		if (!acLists[LIST_MESSAGES].collapsed)
		{
			vertices[0] = { listHeading.right + 5, listHeading.top + 10 };
			vertices[1] = { listHeading.right + 15, listHeading.top + 10 };
			vertices[2] = { listHeading.right + 10, listHeading.top + 3 };
		}
		dc->Polygon(vertices, 3);
	}

	dc->RestoreDC(sDC);
	DeleteObject(targetPen);
	DeleteObject(targetBrush);
}


void CSiTRadar::OnDoubleClickScreenObject(int ObjectType, const char* sObjectId, POINT Pt, RECT Area, int Button)
{
	
}

void CSiTRadar::OnAsrContentToBeSaved() {

}

void CSiTRadar::OnControllerPositionUpdate(CController Controller)
{
	/*std::once_flag flag1;

	std::call_once(flag1, [&]() {

		for (CFlightPlan flightPlan = GetPlugIn()->FlightPlanSelectFirst(); flightPlan.IsValid();
			flightPlan = GetPlugIn()->FlightPlanSelectNext(flightPlan)) {
			auto itr = find_if(menuState.squawkCodes.begin(), menuState.squawkCodes.end(), [&flightPlan](SSquawkCodeManagement& m)->bool {return !strcmp(m.fpcs.c_str(), flightPlan.GetCallsign()); });
			if (itr == menuState.squawkCodes.end()) {
				SSquawkCodeManagement sq;
				sq.fpcs = flightPlan.GetCallsign();
				sq.squawk = flightPlan.GetControllerAssignedData().GetSquawk();
				sq.numCorrelatedRT = 0;
				menuState.squawkCodes.push_back(sq);
			}
			else {
				menuState.squawkCodes.at(distance(menuState.squawkCodes.begin(), itr)).squawk = flightPlan.GetControllerAssignedData().GetSquawk();
			}

		}
		});
	*/

	for (CController ctrl = CSiTRadar::m_pRadScr->GetPlugIn()->ControllerSelectFirst(); ctrl.IsValid(); ctrl = CSiTRadar::m_pRadScr->GetPlugIn()->ControllerSelectNext(ctrl))
	{
		if (ctrl.GetPositionIdentified()) {
			string cjs = ctrl.GetPositionId();
			if (cjs.size() <= 2) {
				CSiTRadar::menuState.nearbyCJS.insert(pair<string, bool>(ctrl.GetPositionId(), false));
			}
		}
	}
}

void CSiTRadar::OnControllerDisconnect(CController Controller) {
	if (CSiTRadar::menuState.nearbyCJS.find(Controller.GetPositionId()) != CSiTRadar::menuState.nearbyCJS.end()) {
		CSiTRadar::menuState.nearbyCJS.erase(Controller.GetPositionId());
	}
}

void CSiTRadar::OnFlightPlanFlightStripPushed(CFlightPlan FlightPlan,
	const char* sSenderController,
	const char* sTargetController) {

	string poString = FlightPlan.GetControllerAssignedData().GetFlightStripAnnotation(0);

	// On receive PO message
	if (!strcmp(sTargetController, m_pRadScr->GetPlugIn()->ControllerMyself().GetCallsign())) {
		if (poString.find("PO") != string::npos) {
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].pointOutToMe = true;
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].pointOutPendingApproval = true;
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].POString = poString.substr(3);
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].POTarget = GetPlugIn()->ControllerSelect(sSenderController).GetPositionId();
		}
		if (poString.empty()) {
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].pointOutToMe = false;
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].POString = "";
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].POTarget = "";
		}
	}

	// Sent PO
	if (!strcmp(sSenderController, m_pRadScr->GetPlugIn()->ControllerMyself().GetCallsign())) {
		if (!poString.empty()) {
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].pointOutFromMe = true;
			CSiTRadar::mAcData[FlightPlan.GetCallsign()].POString = poString.substr(3);
		}
	} 



}
