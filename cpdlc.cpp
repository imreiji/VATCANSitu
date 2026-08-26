#include "pch.h"
#include "cpdlc.h"
#include "CpdlcPacket.h"

unsigned int CPDLCMessage::ids = 0;
std::string CPDLCMessage::hoppieCode = "";
std::string CPDLCMessage::hoppieICAO = "";
std::string CPDLCMessage::cpdlcServer = "https://www.hoppie.nl/acars/system/connect.html";
bool CPDLCMessage::firstPeek = true;

std::string CPDLCMessage::YYMMDDString() {
	// Get the current time in UTC (Zulu time) using std::chrono
	auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	// Buffer to store the result of gmtime_s
	std::tm timeInfoUTC;

	// Use gmtime_s to convert the time to a std::tm structure in UTC
	if (gmtime_s(&timeInfoUTC, &currentTime) != 0) {
		// Handle error (you may want to throw an exception or handle it appropriately)
		return "Error in gmtime_s";
	}

	// Extract year, month, and day components
	int year = timeInfoUTC.tm_year % 100; // Last two digits of the year
	int month = timeInfoUTC.tm_mon + 1;   // Month (0-based)
	int day = timeInfoUTC.tm_mday;        // Day of the month

	// Format the date as "YYMMDD"
	std::ostringstream oss;
	oss << std::setfill('0') << std::setw(2) << year
		<< std::setw(2) << month
		<< std::setw(2) << day;

	// Store the formatted date as a string
	std::string dateString = oss.str();
	return dateString;
}

std::string CPDLCMessage::FreqTruncate(double freq) {

	// Convert the double to a string with fixed precision
	std::ostringstream oss;
	oss << std::fixed << std::setprecision(3) << freq;
	std::string result = oss.str();

	// Remove trailing zeros and the decimal point if unnecessary
	size_t decimalPointPos = result.find_last_of('.');
	result.erase(result.find_last_not_of('0') + 1);
	if (decimalPointPos == result.size() - 1) {
		result.pop_back();  // Remove the trailing decimal point
	}

	return result;
}

CPDLCMessage::CPDLCMessage() {

	this->id = CPDLCMessage::ids; // assign an internal id to every message
	CPDLCMessage::ids++;

}

CPDLCMessage::~CPDLCMessage() {}

CPDLCMessage CPDLCMessage::parseDLMessage(std::string& rawMessage) { // breaks up rawstring and returns a CPDLCMessage object with each message

	CPDLCMessage parsedMessage;


	if (rawMessage.length() > 3) {

		auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		parsedMessage.timeParsed = currentTime;

		// UL messages are stored at creation, so downloaded messages are always DL
		parsedMessage.isdlMessage = true;
		parsedMessage.receipient = CPDLCMessage::hoppieICAO;

		// The caller loops while rawMessage is longer than four characters and relies on
		// this function shortening it every time. Test find()'s result BEFORE adding to
		// it: npos + 1 is 0, so the original
		//
		//     size_t secondBrace = rawMessage.find('}', firstBrace + 1) + 1;
		//     if (firstBrace != npos && secondBrace != npos) { ... }
		//     rawMessage = rawMessage.substr(secondBrace);
		//
		// could never detect a missing closing brace, and fell through to substr(0),
		// leaving the string untouched. A reply beginning "ok " with no '}' in it then
		// spun forever - on the render thread, so EuroScope hung with no way out.
		const size_t firstBrace = rawMessage.find('{');
		const size_t closingBrace = (firstBrace == std::string::npos)
			? rawMessage.find('}')
			: rawMessage.find('}', firstBrace + 1);

		if (closingBrace == std::string::npos) {
			// Nothing parseable left. Consume the remainder so the caller terminates.
			rawMessage.clear();
			return parsedMessage;
		}

		const size_t secondBrace = closingBrace + 1;
		size_t substringStart = 0;
		size_t substringLength = 0;

		if (firstBrace != std::string::npos && firstBrace < closingBrace) {
			substringStart = firstBrace + 1;
			substringLength = secondBrace - substringStart;
		}

		std::string result = rawMessage.substr(substringStart, substringLength);

		// the chopped message is returned by reference and truncated
		rawMessage = rawMessage.substr(secondBrace);

		// substring the hoppie messageID ** this only shows in PEEK **

		if (firstPeek) {
			size_t space = result.find(' ');
			if (space != std::string::npos) {
				parsedMessage.hoppieMessageID = result.substr(0, space);
				result = result.substr(space + 1);
			}
		}

		// get the callsign
		size_t space = result.find(' ');
		if (space != std::string::npos) {
			parsedMessage.sender = result.substr(0, space);
			result = result.substr(space+1);
		}
		// get the type
		space = result.find(' ');
		if (space != std::string::npos) {
			parsedMessage.messageType = result.substr(0, space);
			result = result.substr(space+1);
		}

		if (parsedMessage.messageType == "telex") {

			if (result.length() >= 2) {
				parsedMessage.rawMessageContent = result.substr(1, result.length()-2);
			}
			else {
				parsedMessage.rawMessageContent = "";
			}

		}

		// CPDLC Messages are in this format: {10220055 DLH2RQ cpdlc {/data2/2/19/N/WILCO}}
		// DL messages are coded as "Need response from ATC?" Yes = 'Y' No = 'N'
		// if response is needed, then when printing, can link this to an UP message

		if (parsedMessage.messageType == "cpdlc") {

			// The message is the remainder after the fifth delimiter, not the sixth of
			// six fields. Splitting on '/' and taking a component truncated any message
			// that contained a slash - a frequency pair, an altitude block - and the
			// pop_back() that followed then removed a real character, because the
			// closing brace it assumed was on a later component. See CpdlcPacket.h.
			const SituCpdlcPacket::Packet packet = SituCpdlcPacket::Parse(result);

			if (packet.valid) {

				// Both fields come straight off the network. stoi throws
				// std::invalid_argument on anything non-numeric and std::out_of_range on
				// anything too large, and this runs inside a EuroScope callback, where an
				// escaping exception unwinds through a module boundary.
				try {
					if (!packet.minField.empty()) {
						parsedMessage.messageID = stoi(packet.minField);
					}
					if (!packet.mrnField.empty()) {
						parsedMessage.responseToMessageID = stoi(packet.mrnField);
					}
				}
				catch (const std::exception&) {
					parsedMessage.messageID = -1;
					parsedMessage.responseToMessageID = -1;
					parsedMessage.rawMessageContent = "INVALID DOWNLINK MESSAGE";
					return parsedMessage;
				}

				parsedMessage.responseRequired = packet.responseAttribute;
				parsedMessage.rawMessageContent = packet.message;

				// All D/L messages open mnemonic -> parsed in the display part
				parsedMessage.opensMnemonic = true;
			}
			else {
				parsedMessage.rawMessageContent = "INVALID DOWNLINK MESSAGE";
			}
		}
	}

	return parsedMessage;
}

// Percent-encodes a value for use in a query string or POST body.
//
std::string CPDLCMessage::PollCPDLCMessages() { // Returns raw string of CPDLC messages; Should be called every 50-70s to get new messages
	// https, not http. The logon code is a per-user credential and was previously sent in
	// clear text, as a URL query parameter, every sixty seconds for the whole session -
	// readable by anything on the network path and recorded by any intermediary that logs
	// request lines. The endpoint serves https with a valid certificate.
	std::string url;
	url = CPDLCMessage::cpdlcServer + "?logon=" + SituUrl::Encode(CPDLCMessage::hoppieCode)
		+ "&from=" + SituUrl::Encode(CPDLCMessage::hoppieICAO) + "&to=SERVER";

	if (CPDLCMessage::firstPeek) {
		url += "&type=peek";
	} else { 
		url += "&type=poll";
	}

	const SituHttp::Response poll = SituHttp::Get(url, 2500);
	if (!poll.ok) {
		return "Error: Hoppie poll failed - " + poll.error;
	}

	return poll.body;
}

void CPDLCMessage::SendCPDLCMessage() {

	// https, and every value percent-encoded - see PollCPDLCMessages and SituUrl::Encode.
	const std::string url = CPDLCMessage::cpdlcServer;

	std::string postfields = "logon=";
	postfields += SituUrl::Encode(this->hoppieCode);
	postfields += "&from=";
	postfields += SituUrl::Encode(this->hoppieICAO);
	postfields += "&to=";
	postfields += SituUrl::Encode(this->receipient);

	if (this->messageType == "cpdlc") {
		// The packet is one field, so encode it whole once it is assembled. Encoding the
		// pieces separately would escape the '/' separators that give it its structure.
		std::string packet = "/data2/";
		packet += std::to_string(this->messageID);
		packet += "/";
		if (this->responseToMessageID != -1) {
			packet += std::to_string(this->responseToMessageID);
		}
		packet += "/";
		packet += this->responseRequired;
		packet += "/";
		packet += this->rawMessageContent;

		postfields += "&type=cpdlc";
		postfields += "&packet=";
		postfields += SituUrl::Encode(packet);
	}

	if (this->messageType == "telex") {
		postfields += "&type=telex";
		postfields += "&packet=";
		postfields += SituUrl::Encode(this->rawMessageContent);
	}

	// The send had no timeout at all, so a hung gateway blocked whoever called it for as
	// long as the OS allowed. Five seconds, matching the poll's order of magnitude.
	const SituHttp::Response post = SituHttp::Post(url, postfields, 5000);

	// Hoppie answers "ok" on success. Anything else - a transport failure, a non-2xx, or
	// a 200 carrying an error string - leaves the message unsent so it is not treated as
	// delivered.
	this->sent = post.ok && post.body.compare(0, 2, "ok") == 0;

}

void CPDLCMessage::GenerateReply(CPDLCMessage originalMessage) {

	this->receipient = originalMessage.sender;
	this->responseToMessageID = originalMessage.messageID;
	this->isdlMessage = !this->isdlMessage; // replies are the opposite type

}

std::string CPDLCMessage::MakePDCMessage(EuroScopePlugIn::CFlightPlan& flightplan, EuroScopePlugIn::CController& controller, std::string atisLetter, std::string subtype) {


	this->sender = this->hoppieICAO;
	this->receipient = flightplan.GetCallsign();

	// Generate some hash number to generate a PDC identifier
	std::hash<std::string> hashFunction;
	size_t seedValue = hashFunction(this->receipient);

	// Convert the hash value to an integer (or use it directly)
	unsigned int seedInt = static_cast<unsigned int>(seedValue);

	char letter = 'A'; // filler letter for now
	std::string identifierLetter;
	unsigned int pdcNumbers = (static_cast<unsigned int>(this->timeParsed) + seedInt) % 900 + 100; // 3 digit number, generate with hoppie ID
	letter += (static_cast<unsigned int>(this->timeParsed) + seedInt) % 25;
	identifierLetter = letter;


	// truncate the route if too long
	std::string rteStr = flightplan.GetFlightPlanData().GetRoute();
	const size_t nCharacters = 52;

	if (rteStr.length() > nCharacters) {

		// Break at the first space at or after the limit, so the route is cut between
		// waypoints rather than through one.
		const size_t space = rteStr.find(' ', nCharacters);

		// A route with no space after the limit is one long token with no clean break.
		// Cut it at the limit: this used to leave substringBeforeSpace empty and send
		// "// FILED ROUTE" with no route in front of it at all.
		rteStr = rteStr.substr(0, (space == std::string::npos) ? nCharacters : space) + "// FILED ROUTE";
	}
	else if (rteStr.length() == nCharacters) {
		rteStr += "// FILED ROUTE";
	}

	if (subtype == "FSM") {

		this->responseRequired = "NE";
		this->messageType = "cpdlc";

		if (flightplan.IsValid()) {

			this->rawMessageContent = "FSM "; // Generate the PDC string;
			this->rawMessageContent += ZuluTimeStringGen();
			this->rawMessageContent += " ";
			this->rawMessageContent += YYMMDDString();
			this->rawMessageContent += " ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetOrigin();
			this->rawMessageContent += "RCD RECEIVED @REQUEST BEING PROCESSED @STANDBY";

			return "CDAPending";
		}
		else {
			this->rawMessageContent = "RCD REJECTED @FLIGHT PLAN NOT HELD @REVERT TO VOICE PROCEDURES";

			return "RCDRejected";
		}

	}
	else {

		//ARINC 623 Messages

		if (flightplan.GetFlightPlanData().GetOrigin() == "CYTZ") {

			this->responseRequired = "WU";
			this->messageType = "cpdlc";
			/*
				this->rawMessageContent = "CLD "; // Generate the PDC string;
				this->rawMessageContent += ZuluTimeStringGen();
				this->rawMessageContent += " ";
				this->rawMessageContent += YYMMDDString();
				this->rawMessageContent += " ";
			*/

			this->rawMessageContent += flightplan.GetFlightPlanData().GetOrigin();
			this->rawMessageContent += " PDC ";
			this->rawMessageContent += std::to_string(pdcNumbers);
			this->rawMessageContent += " ";
			this->rawMessageContent += flightplan.GetCallsign();
			this->rawMessageContent += " CLRD TO ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDestination();
			this->rawMessageContent += " OFF ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDepartureRwy();
			this->rawMessageContent += " VIA ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetSidName();
			this->rawMessageContent += " SQUAWK ";
			this->rawMessageContent += flightplan.GetControllerAssignedData().GetSquawk();
			this->rawMessageContent += " ATIS ";
			this->rawMessageContent += atisLetter;
			this->rawMessageContent += " CONTACT ";
			this->rawMessageContent += controller.GetCallsign();
			this->rawMessageContent += " ON FREQ ";
			this->rawMessageContent += FreqTruncate(controller.GetPrimaryFrequency());

			return "CDAPending";
		}

		else if (flightplan.GetFlightPlanData().GetOrigin() == "CYUL") {

			this->responseRequired = "WU";
			this->messageType = "cpdlc";

			this->rawMessageContent += flightplan.GetFlightPlanData().GetOrigin();
			this->rawMessageContent = " PDC "; // Generate the PDC string;
			this->rawMessageContent += std::to_string(pdcNumbers);
			this->rawMessageContent += " ";

			this->rawMessageContent += flightplan.GetCallsign();
			this->rawMessageContent += " CLRD TO ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDestination();
			this->rawMessageContent += " OFF ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDepartureRwy();
			this->rawMessageContent += " VIA ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetSidName();
			this->rawMessageContent += " SQUAWK ";
			this->rawMessageContent += flightplan.GetControllerAssignedData().GetSquawk();
			this->rawMessageContent += " NEXT FREQ ";
			this->rawMessageContent += FreqTruncate(controller.GetPrimaryFrequency());
			this->rawMessageContent += " ATIS ";
			this->rawMessageContent += atisLetter;

			return "CDAPending";

		}

		// ARINC 622 

		// CPDLC only available in CYTZ and CYYZ in CZYZ if someone requests from elsewhere send
		// ARINC 620/622 simulate a telex message
		/* -// ATC PA01 YYZOWAC 22JUN/1003 C-FITW/733/AC7281

			TIMESTAMP 22JUN21 10:03
			*PRE-DEPARTURE CLEARANCE*
			FLT ACA7281 CYYZ
			H/B77W/W FILED FL360
			XPRD 2264
			USE SID AVSEP6
			DEPARTURE RUNWAY 33R
			DESTINATION CYVR
			*** ADVISE ATC IF RUNUP REQUIRED ***
			CONTACT CLEARANCE WITH IDENTIFIER 360M
			AVSEP6 MUSIT SSM YQT GERTY
			PEMPA AXILI BOOTH CANUC5
			END

			*/

		else {

			this->rawMessageContent = "TIMESTAMP ";
			this->rawMessageContent += ZuluTimeStringGen();
			this->rawMessageContent += " *PRE-DEPARTURE CLEARANCE* FLT ";
			this->rawMessageContent += flightplan.GetCallsign();
			this->rawMessageContent += " ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetOrigin();
			this->rawMessageContent += " ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetAircraftFPType();
			this->rawMessageContent += " FILED";
			if (flightplan.GetFlightPlanData().GetFinalAltitude() > 18000) {
				this->rawMessageContent += " FL";
				this->rawMessageContent += std::to_string(flightplan.GetFlightPlanData().GetFinalAltitude() / 100);
			}
			else {
				this->rawMessageContent += std::to_string(flightplan.GetFlightPlanData().GetFinalAltitude());
			}
			this->rawMessageContent += " XPRD ";
			this->rawMessageContent += flightplan.GetControllerAssignedData().GetSquawk();
			this->rawMessageContent += " USE SID ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetSidName();
			this->rawMessageContent += " DEPARTURE RUNWAY ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDepartureRwy();
			this->rawMessageContent += " DESTINATION ";
			this->rawMessageContent += flightplan.GetFlightPlanData().GetDestination();
			this->rawMessageContent += " *** ADVISE ATC IF RUNUP REQUIRED *** ";
			this->rawMessageContent += "CONTACT ATC WITH IDENTIFIER ";
			this->rawMessageContent += std::to_string(pdcNumbers);
			this->rawMessageContent += identifierLetter;
			this->rawMessageContent += " ";
			this->rawMessageContent += rteStr;
			this->rawMessageContent += " END";

			this->messageType = "telex";

		}
		std::string FPUI = std::to_string(pdcNumbers) + identifierLetter;
		return FPUI;
	}
}

void CPDLCMessage::processMessage() { // should loop with every Poll try resending messages or automatically generate responses where appropriate




	if (!this->isdlMessage) {
		if (!this->sent) {

			// try resending UL messages if failed at last push;
			this->SendCPDLCMessage();
			this->sent = true;

		}

	}

	// Put garbage cleaning;

}

// "HH:MM" for display next to a message. ZuluTimeStringGen is the wire format, "HHMM"
// for the current time; this one formats a time we already hold.
std::string CPDLCMessage::ZuluTimeFormated(std::time_t time) {

	std::tm timeInfoUTC;

	if (gmtime_s(&timeInfoUTC, &time) != 0) {
		return "--:--";
	}

	std::ostringstream oss;
	oss << std::put_time(&timeInfoUTC, "%H:%M");
	return oss.str();
}

std::string CPDLCMessage::ZuluTimeStringGen() {

	auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

	// Buffer to store the result of gmtime_s
	std::tm timeInfoUTC;

	// Use gmtime_s to convert the time to a std::tm structure in UTC
	if (gmtime_s(&timeInfoUTC, &currentTime) != 0) {
		// Handle error (you may want to throw an exception or handle it appropriately)
		return "Error in gmtime_s";
	}

	// Format the time as "HHMM"
	std::ostringstream oss;
	oss << std::setfill('0') << std::setw(2) << timeInfoUTC.tm_hour
		<< std::setw(2) << timeInfoUTC.tm_min;

	// Store the formatted time as a string
	std::string zuluTimeString = oss.str();
	return zuluTimeString;
}

bool CPDLCMessage::isValidDLMessage() {

	std::vector<std::regex> acceptableMessages = {



	};

	bool isMatch = false;
	for (const auto& pattern : acceptableMessages) {
		if (std::regex_match(this->rawMessageContent, pattern)) {
			return true;
		}
	}
	return false;
}

/* enum CpdlcMessageExpectedResponseType {
	NotRequired = 'NE',
	WilcoUnable = 'WU',
	AffirmNegative = 'AN',
	Roger = 'R',
	No = 'N',
	Yes = 'Y'
}
*/