#pragma once

#include "HttpClient.h"
#include "UrlEncode.h"
#include "CpdlcDcl.h"
#include <EuroScopePlugIn.h>
#include <string>
#include <sstream>
#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <vector>
#include <regex>

class CPDLCMessage {

private:


public:
	static std::string hoppieCode;  // Hoppie Logon Code
	static std::string hoppieICAO;

	// Where CPDLC is sent. Settable so the plugin can be pointed at a local
	// distributor holding one Hoppie connection for a whole sector, rather than every
	// controller holding their own. TopSky exposes the same seam as CPDLC_Server.
	static std::string cpdlcServer;

	// Departure clearance templates, read once from the [DCL] section of
	// SituCPDLC.txt. Static because the screens share one copy and it never changes
	// at run time.
	static SituCpdlcDcl::Table dclTable;
	static unsigned int CPDLCMessage::ids;
	static bool firstPeek; //


	int id;

	bool isdlMessage; // if not DL message, then it is UL message
	bool isCompleted{ false }; // checkmark if completed
	bool sent{ false };
	bool opensMnemonic{ false };

	// Initialised, because MakePDCMessage folds it into the PDC identifier arithmetic.
	// parseDLMessage assigns it for downlinks and one uplink path assigns it too, but a
	// default-constructed message otherwise carried whatever was on the stack - undefined
	// behaviour, and a non-deterministic identifier where it happened to work. Every other
	// member of this class is brace-initialised.
	std::time_t timeParsed{ 0 }; // time that message was created
	std::string hoppieMessageID{""}; // from the api, empty string if doesn't exist
	std::string messageType{""}; // telex or CPDLC

	std::string sender{ "" };
	std::string receipient{ "" };
	int messageID{ -1 };
	int responseToMessageID{ -1 };
	std::string responseRequired{ "" };
	std::string rawMessageContent{ "" };

	unsigned int ARINCmessageType; // standardized ARINC message type for CPDLC

	CPDLCMessage();
	~CPDLCMessage();

	static std::string CPDLCMessage::PollCPDLCMessages();
	std::string MakePDCMessage(EuroScopePlugIn::CFlightPlan& flightplan, EuroScopePlugIn::CController& controller, std::string atisLetter, std::string subtype = "");
	void GenerateReply(CPDLCMessage originalMessage);
	void SendCPDLCMessage();
	static CPDLCMessage parseDLMessage(std::string& rawMessage);
	void processMessage(); 
	bool isValidDLMessage(); // Per AIP , which messages are allowable, returns false if not standard telex


	// Formating Helpers

	static std::string ZuluTimeStringGen();
	static std::string FreqTruncate(double freq);
	static std::string YYMMDDString();
	static std::string ZuluTimeFormated(std::time_t time);
};