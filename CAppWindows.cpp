#include "pch.h"
#include "CAppWindows.h"

unsigned long CAppWindows::windowIDs_ = 0;
unsigned long SListBoxElement::m_elementIDcount = 0;
unsigned long STextField::m_textFieldIDcount = 0;
unsigned long SListBox::m_list_box_ids;

CAppWindows::CAppWindows()
{
	
}

CAppWindows::CAppWindows(POINT origin, int winType, CFlightPlan& fp, RECT radarea, const vector<CPDLCMessage>& cpdlcmsgs) {
	m_origin = origin;
	m_winType = winType;
	m_windowId_ = windowIDs_;
	string cs = fp.GetCallsign();

	if (winType == WINDOW_CPDLC_EDITOR) {
		windowTitle = "CPDLC Message Editor - " + cs;
		m_width = 500;
		m_height = 250;

		SWindowText s;

		s.text = "Open Downlink Dialogues";
		s.location = { 5, 22 };
		m_text_.push_back(s);

		s.text = "ID";
		s.location = { 12, 34 };
		m_text_.push_back(s);

		s.text = "Time";
		s.location = { 34, 34 };
		m_text_.push_back(s);

		s.text = "Text";
		s.location = { 84, 34 };
		m_text_.push_back(s);

		s.text = "Uplink Messages";
		s.location = { 5, 120 };
		m_text_.push_back(s);

		s.text = "Text";
		s.location = { 14, 132 };
		m_text_.push_back(s);

		SWindowButton b;
		b.location = { 430, 219 };
		b.m_height = 25;
		b.m_width = 60;
		b.text = "Send";
		b.windowID = m_windowId_;
		m_buttons_.push_back(b);

		// The downlink being replied to, and the uplink being composed.
		STextField stf;
		stf.m_parentWindowID = m_windowId_;
		stf.m_width = 488;
		stf.m_height = 72;
		stf.m_location_ = { 5, 48 };
		stf.m_textfield_type = TEXTFIELD_CPDLC_MESSAGE;
		m_textfields_.push_back(stf);

		stf.m_location_ = { 5, 145 };
		stf.m_textfield_type = TEXTFIELD_CPDLC_PENDING_UPLINK;
		m_textfields_.push_back(stf);
	}

	if (winType == WINDOW_CPDLC) {
		windowTitle = "CPDLC - " + cs;
		m_width = 420;
		m_height = 253;

		// Standard replies on one row, then the message categories in a 5x3 grid below.
		// Blank labels are grid positions with nothing assigned yet; they are laid out
		// anyway so the grid does not reflow when one is filled in.
		static const char* const replies[5] = { "Unable", "Roger", "Affirm", "Negative", "Deferred" };
		static const char* const categories[3][5] = {
			{ "Standby", "Radio", "Altitude", "Speed", "Route" },
			{ "Radar",   "",      "",         "",      ""      },
			{ "Misc",    "",      "",         "",      "PDC"   },
		};

		for (int i = 0; i < 5; i++) {
			SWindowButton b;
			b.location = { (5 + i * 65), 161 };
			b.m_height = 25;
			b.m_width = 64;
			b.windowID = m_windowId_;
			b.m_textcolor = C_CPDLC_GREEN;
			b.text = replies[i];
			m_buttons_.push_back(b);
		}
		for (int i = 0; i < 5; i++) {
			for (int j = 0; j < 3; j++) {
				SWindowButton b;
				b.location = { (5 + i * 65), 186 + j * 20 };
				b.m_height = 20;
				b.m_width = 64;
				b.windowID = m_windowId_;
				b.m_textcolor = C_CPDLC_GREEN;
				b.text = categories[j][i];
				m_buttons_.push_back(b);
			}
		}

		SWindowButton close, flightplan, closedialog, endservice, connect;

		close.location = { 333, 221 };
		close.m_height = 25;
		close.m_width = 80;
		close.text = "Close";
		close.windowID = m_windowId_;

		flightplan.location = { 333, 192 };
		flightplan.m_height = 25;
		flightplan.m_width = 80;
		flightplan.text = "Flight Plan";
		flightplan.windowID = m_windowId_;

		closedialog.location = { 333, 137 };
		closedialog.m_height = 25;
		closedialog.m_width = 80;
		closedialog.text = "Close Dialog";
		closedialog.windowID = m_windowId_;

		endservice.location = { 333, 82 };
		endservice.m_height = 25;
		endservice.m_width = 80;
		endservice.text = "End Service";
		endservice.windowID = m_windowId_;

		connect.location = { 333, 55 };
		connect.m_height = 25;
		connect.m_width = 80;
		connect.text = "Connect";
		connect.windowID = m_windowId_;

		m_buttons_.push_back(close);
		m_buttons_.push_back(flightplan);
		m_buttons_.push_back(closedialog);
		m_buttons_.push_back(endservice);
		m_buttons_.push_back(connect);

		SListBox lb;
		lb.m_max_elements = 8;
		lb.m_height = lb.m_max_elements * CPDLC_ROW_HEIGHT;
		lb.m_width = 328;
		lb.m_origin = { 0, 0 };
		lb.m_windowID_ = m_windowId_;
		lb.PopulateCPDLCListBox(cpdlcmsgs);
		m_listboxes_.emplace_back(lb);
	}

	m_origin.x = origin.x - m_width / 2;
	m_origin.y = origin.y - m_height / 2;

	if (m_origin.x < radarea.left) { m_origin.x = radarea.left; }
	if ((m_origin.x + m_width) > radarea.right) { m_origin.x = radarea.right - m_width; }
	if (m_origin.y < radarea.top + 60) { m_origin.y = radarea.top + 60; }
	if ((m_origin.y + m_height) > (radarea.bottom)) { m_origin.y = radarea.bottom - m_height; }

	windowIDs_++;
}

CAppWindows::CAppWindows(POINT origin, int winType, RECT radarea) {
	m_origin = origin;
	m_winType = winType;
	m_windowId_ = windowIDs_;

	if (winType == WINDOW_FREE_TEXT) {
		windowTitle = "Free Text";
		m_width = 300;
		m_height = 100;
		SListBox lb;

		SWindowButton submit, cancel;

		submit.location = { 90, 60 };
		submit.m_height = 25;
		submit.m_width = 60;
		submit.text = "Submit";
		submit.windowID = m_windowId_;

		cancel.location = { 155, 60 };
		cancel.m_height = 25;
		cancel.m_width = 60;
		cancel.text = "Cancel";
		cancel.windowID = m_windowId_;

		m_buttons_.push_back(submit);
		m_buttons_.push_back(cancel);

		STextField freetext;
		freetext.m_location_ = { 16, 30 };
		freetext.m_height = 20;
		freetext.m_width = 268;
		freetext.m_parentWindowID = m_windowId_;
		m_textfields_.push_back(freetext);
	}

	m_origin.x = origin.x - m_width / 2;
	m_origin.y = origin.y - m_height / 2;

	if (origin.x < radarea.left) { m_origin.x = radarea.left; }
	if ((origin.x + m_width) > radarea.right) { m_origin.x = radarea.right - m_width; }
	if (origin.y < radarea.top + 60) { m_origin.y = radarea.top + 60; }
	if ((origin.y + m_height) > (radarea.bottom)) { m_origin.y = radarea.bottom - m_height; }

	windowIDs_++;
}

CAppWindows::CAppWindows(POINT origin, int winType, CFlightPlan fp, RECT radarea, vector<string>* lbElements) {
	m_origin = origin;
	m_winType = winType;
	m_windowId_ = windowIDs_;
	m_callsign = fp.GetCallsign();
	string s;
	s = fp.GetCallsign();

	if (winType == WINDOW_CTRL_REMARKS) {
		s += " Ctrl Remarks";
		windowTitle = s.c_str();
		m_width = 300;
		m_height = 250;
		SListBox lb;
		lb.PopulateListBox(*lbElements);
		m_listboxes_.emplace_back(lb);

		SWindowButton blank, submit, cancel;

		submit.location = { 90, 210 };
		submit.m_height = 25;
		submit.m_width = 60;
		submit.text = "Submit";
		submit.windowID = m_windowId_;

		cancel.location = { 155, 210 };
		cancel.m_height = 25;
		cancel.m_width = 60;
		cancel.text = "Cancel";
		cancel.windowID = m_windowId_;

		blank.location = { 120, 22 };
		blank.m_height = 25;
		blank.m_width = 60;
		blank.text = "Blank";
		blank.windowID = m_windowId_;

		m_buttons_.push_back(submit);
		m_buttons_.push_back(blank);
		m_buttons_.push_back(cancel);

		STextField freetext;
		freetext.m_location_ = { 16, 188 };
		freetext.m_height = 20;
		freetext.m_width = 268;
		freetext.m_parentWindowID = m_windowId_;
		m_textfields_.push_back(freetext);
	}

	m_origin.x = origin.x - m_width / 2;
	m_origin.y = origin.y - m_height / 2;

	if (origin.x < radarea.left) { m_origin.x = radarea.left; }
	if ((origin.x + m_width) > radarea.right) { m_origin.x = radarea.right - m_width; }
	if (origin.y < radarea.top + 60) { m_origin.y = radarea.top + 60; }
	if ((origin.y + m_height) > (radarea.bottom)) { m_origin.y = radarea.bottom - m_height; }

	windowIDs_++;
}

CAppWindows::CAppWindows(POINT origin, int winType, CFlightPlan fp, RECT radarea, ACRoute* rte) {
	m_origin = origin;
	m_winType = winType;
	m_windowId_ = windowIDs_;
	m_callsign = fp.GetCallsign();
	string s; 

	if (winType == WINDOW_DIRECT_TO) {
		windowTitle = m_callsign;
		m_height = 190;
		m_width = 110;

		SListBox lb;
		lb.m_max_elements = 5;
		lb.PopulateDirectListBox(rte, fp);
		lb.m_origin = m_origin;
		m_listboxes_.emplace_back(lb);

		SWindowButton submit, cancel;

		submit.location = { 6, 155 };
		submit.m_height = 25;
		submit.m_width = 45;
		submit.text = "Ok";
		submit.windowID = m_windowId_;

		cancel.location = { 56, 155 };
		cancel.m_height = 25;
		cancel.m_width = 45;
		cancel.text = "Cancel";
		cancel.windowID = m_windowId_;

		m_buttons_.push_back(submit);
		m_buttons_.push_back(cancel);

	}

	m_origin.x = origin.x - m_width / 2;
	m_origin.y = origin.y - m_height / 2;

	if (origin.x < radarea.left) { m_origin.x = radarea.left; }
	if ((origin.x + m_width) > radarea.right) { m_origin.x = radarea.right - m_width; }
	if (origin.y < radarea.top + 60) { m_origin.y = radarea.top + 60; }
	if ((origin.y + m_height) > (radarea.bottom)) { m_origin.y = radarea.bottom - m_height; }

	windowIDs_++;
}

CAppWindows::CAppWindows(POINT origin, int winType, CFlightPlan fp, RECT radarea) {
	m_origin = origin;
	m_winType = winType;
	m_windowId_ = windowIDs_;
	m_callsign = fp.GetCallsign();
	string s;
	s = fp.GetCallsign();

	if (winType == WINDOW_HANDOFF_EXT_CJS) {
		s += " H/O:";
		windowTitle = s.c_str();
		m_width = 105;
		m_height = 50;

		SWindowButton cancel;
		cancel.location = { 40, 24 };
		cancel.m_height = 20;
		cancel.m_width = 50;
		cancel.text = "Cancel";
		cancel.windowID = m_windowId_;
		m_buttons_.push_back(cancel);

		STextField cjsText;
		cjsText.m_location_ = { 8,24 };
		cjsText.m_height = 19;
		cjsText.m_width = 30;
		cjsText.m_focused = true;
		cjsText.m_parentWindowID = m_windowId_;
		m_textfields_.push_back(cjsText);


	}

	if (winType == WINDOW_POINT_OUT) {
		windowTitle = "Point Out";
		m_height = 85;
		m_width = 210;

		SWindowButton submit, cancel;

		submit.location = { 50, 50 };
		submit.m_height = 25;
		submit.m_width = 60;
		submit.text = "Submit";
		submit.windowID = m_windowId_;

		cancel.location = { 112, 50 };
		cancel.m_height = 25;
		cancel.m_width = 60;
		cancel.text = "Cancel";
		cancel.windowID = m_windowId_;

		m_buttons_.push_back(submit);
		m_buttons_.push_back(cancel);

		STextField cjsText;
		cjsText.m_location_ = { 8,28 };
		cjsText.m_height = 19;
		cjsText.m_width = 38;
		cjsText.m_parentWindowID = m_windowId_;
		m_textfields_.push_back(cjsText);

		STextField poMessage;
		poMessage.m_location_ = { 48,28 };
		poMessage.m_height = 19;
		poMessage.m_width = 152;
		poMessage.m_parentWindowID = m_windowId_;
		m_textfields_.push_back(poMessage);
	}

	m_origin.x = origin.x - m_width / 2;
	m_origin.y = origin.y - m_height / 2;

	if (origin.x < radarea.left) { m_origin.x = radarea.left; }
	if ((origin.x + m_width) > radarea.right) { m_origin.x = radarea.right - m_width; }
	if (origin.y < radarea.top + 60) { m_origin.y = radarea.top + 60; }
	if ((origin.y + m_height) > (radarea.bottom)) { m_origin.y = radarea.bottom - m_height; }

	windowIDs_++;
}

SWindowElements CAppWindows::DrawWindow(CDC* dc) {
	SWindowElements w;
	
	int sDC = dc->SaveDC();

	dc->SelectObject(CFontHelper::Segoe14);
	dc->SetTextColor(RGB(230, 230, 230));

	//default is unpressed state
	COLORREF pressedcolor = RGB(66, 66, 66);
	COLORREF pcolortl = RGB(140, 140, 140);
	COLORREF pcolorbr = RGB(55, 55, 55);

	COLORREF targetPenColor = RGB(66, 66, 66);
	HPEN targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
	HBRUSH targetBrush = CreateSolidBrush(pressedcolor);

	dc->SelectObject(targetBrush);
	dc->SelectObject(targetPen);

	// Draw Title

	RECT windowRect = { m_origin.x, m_origin.y , m_origin.x + m_width, m_origin.y + m_height };
	RECT titleRect = { m_origin.x, m_origin.y , m_origin.x + m_width, m_origin.y + 24 };

	dc->Rectangle(&windowRect);
	dc->Draw3dRect(&windowRect, C_MENU_GREY4, C_MENU_GREY2);
	InflateRect(&windowRect, -3, -3);
	dc->Draw3dRect(&windowRect, C_MENU_GREY2, C_MENU_GREY4);

	dc->DrawText(this->windowTitle.c_str(), &titleRect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
	InflateRect(&titleRect, -3, -3);
	dc->Draw3dRect(&titleRect, C_MENU_GREY2, C_MENU_GREY4);

	// Draw Secondary title if present

	int listboxDeltaY = 0;
	int listboxDeltaX = 0;
	if (m_winType == WINDOW_CTRL_REMARKS) {
		listboxDeltaY = 25;
	}
	if (m_winType == WINDOW_DIRECT_TO) {
		listboxDeltaX = -10;
	}
	const bool cpdlcList = (m_winType == WINDOW_CPDLC || m_winType == WINDOW_CPDLC_EDITOR);
	// The scroll bar hangs off the right edge of the list. The CPDLC list is inset by
	// three pixels relative to the others, so its scroll bar follows.
	const int scrollbarDeltaX = cpdlcList ? -3 : 0;

	// Draw List Box if present
	for (auto& lb : this->m_listboxes_) {
		lb.m_dc = dc;
		if (cpdlcList) {
			// The CPDLC list sits at its own offset inside the window rather than
			// directly under the title bar, so it carries its own origin.
			lb.RenderCPDLCListBox({ m_origin.x, titleRect.bottom + 2 + lb.m_origin.y });
		}
		else {
			lb.RenderListBox(1, 1, 1, { m_origin.x + listboxDeltaX, titleRect.bottom + 2 + listboxDeltaY});
		}
		if (lb.m_has_scroll_bar) {
			lb.m_scrbar.m_max_elements = lb.m_max_elements;
			lb.m_scrbar.m_origin = { m_origin.x + lb.m_width + 9 + scrollbarDeltaX, titleRect.bottom + 2 + listboxDeltaY };
			lb.m_scrbar.Draw(dc);
		}
	}

	// Draw Buttons if present
	for (auto& but : this->m_buttons_) {
		but.m_dc = dc;
		but.RenderButton(m_origin);
	}

	// Draw textfields if present
	for (auto& textf : this->m_textfields_) {
		textf.RenderTextField(dc, m_origin);
	}

	// Draw static labels if present
	for (auto& text : this->m_text_) {
		text.RenderText(dc, m_origin);
	}

	// restore, then delete: DeleteObject fails on a still-selected object and leaks it
	dc->RestoreDC(sDC);
	DeleteObject(targetPen);
	DeleteObject(targetBrush);

	w.titleBarRect = titleRect;
	return w;
	}

// A CPDLC exchange is stored flat - every message is a separate entry - but it is drawn
// as a dialogue: the message that opened it, then any replies indented beneath it. This
// walks the flat list once and returns the rows in the order they are drawn, so the row
// count used to size the scroll bar and the rows actually painted cannot disagree.
//
// Upstream did the same pairing twice, once to count and once to draw, with the drawing
// pass applying an extra cut that the counting pass did not - so a list with replies in
// it sized its scroll bar for more rows than it ever painted.
std::vector<SListBox::CPDLCRow> SListBox::FlattenCPDLCRows() {
	// These are drawn under the message they answer, never on their own line.
	static const char* const cannedReplies[] = {
		"WILCO", "UNABLE", "NEGATIVE", "STANDBY", "ROGER", "AFFIRM", "LOGON ACCEPTED"
	};

	std::vector<CPDLCRow> rows;
	int stripe = 0;

	// Newest first.
	for (auto it = listBox_.rbegin(); it != listBox_.rend(); ++it) {
		const CPDLCMessage& msg = it->m_cpdlc_message;

		bool isReply = false;
		for (const char* canned : cannedReplies) {
			if (msg.rawMessageContent == canned) { isReply = true; break; }
		}
		if (isReply) { continue; }

		CPDLCRow parent{ &*it, false, stripe, false };

		// A message with no MIN cannot be answered, so it can neither collect replies
		// nor hold a dialogue open.
		if (msg.messageID != -1) {
			bool answered = false;
			std::vector<SListBoxElement*> replies;
			for (auto& candidate : listBox_) {
				if (candidate.m_cpdlc_message.responseToMessageID != msg.messageID) { continue; }
				answered = true;
				// A telex reply closes the dialogue but is not drawn as part of it.
				if (candidate.m_cpdlc_message.messageType == "cpdlc") { replies.push_back(&candidate); }
			}

			// A downlink asking for a reply, or an uplink that requires one, stays open
			// until something answers it.
			const bool awaitingReply = msg.isdlMessage
				? (msg.responseRequired == "Y")
				: (msg.responseRequired == "WU" || msg.responseRequired == "R" || msg.responseRequired == "AN");
			parent.dialogueOpen = !answered && awaitingReply;

			rows.push_back(parent);
			for (SListBoxElement* reply : replies) {
				rows.push_back({ reply, true, stripe, false });
			}
		}
		else {
			rows.push_back(parent);
		}

		stripe++;
	}
	return rows;
}

void SListBox::RenderCPDLCListBox(POINT winOrigin) {
	int sDC = m_dc->SaveDC();

	m_dc->SelectObject(CFontHelper::EuroscopeFixed14);
	m_dc->SetTextColor(C_MENU_TEXT_WHITE);

	HPEN penGrey1 = CreatePen(PS_SOLID, 1, C_MENU_GREY1);
	HPEN penGrey2 = CreatePen(PS_SOLID, 1, C_MENU_GREY2);
	HBRUSH brushGrey1 = CreateSolidBrush(C_MENU_GREY1);
	HBRUSH brushGrey2 = CreateSolidBrush(C_MENU_GREY2);
	HBRUSH brushSelected = CreateSolidBrush(C_MENU_GREY4);

	// Only drawn rows get a hit rectangle. Clear them all first so a row that has been
	// scrolled out of view does not keep catching clicks where it used to be.
	for (auto& element : listBox_) {
		SetRectEmpty(&element.m_ListBoxRect);
	}

	const std::vector<CPDLCRow> rows = FlattenCPDLCRows();
	m_last_element = static_cast<int>(rows.size());

	m_has_scroll_bar = m_last_element > m_max_elements;
	if (m_has_scroll_bar) {
		SListBoxScrollBar scrollBar(m_height, 10, m_ListBoxID, { m_origin.x - 3, m_origin.y }, m_LB_firstElem_idx, (m_last_element - m_max_elements) + 1);
		scrollBar.m_height = m_height;
		scrollBar.m_slider_height_ratio = (double)m_max_elements / (double)m_last_element;
		scrollBar.m_total_elements = m_last_element;
		m_scrbar = scrollBar;
	}

	// Narrower when the scroll bar is taking up the right hand edge.
	const int rowWidth = m_has_scroll_bar ? 316 : 330;
	m_width = rowWidth;

	const int firstRow = m_LB_firstElem_idx;
	// Not std::min: windows.h defines min as a macro, which swallows the qualification.
	int lastRow = firstRow + m_max_elements;
	if (lastRow > m_last_element) { lastRow = m_last_element; }

	int deltay = 0;
	for (int i = firstRow; i < lastRow; i++) {
		const CPDLCRow& row = rows[i];
		SListBoxElement& element = *row.element;
		const CPDLCMessage& msg = element.m_cpdlc_message;

		element.m_width = rowWidth;

		// Zebra stripe by dialogue, not by row, so a message and its replies read as
		// one block.
		const bool lightRow = (row.stripe % 2 != 0);
		m_dc->SelectObject(lightRow ? penGrey2 : penGrey1);
		m_dc->SelectObject(lightRow ? brushGrey2 : brushGrey1);

		if (element.m_selected_) {
			m_dc->SetTextColor(C_MENU_GREY1);
			m_dc->SelectObject(brushSelected);
		}
		else if (row.dialogueOpen) {
			m_dc->SetTextColor(msg.isdlMessage ? C_CPDLC_BLUE : C_CPDLC_GREEN);
		}
		else {
			m_dc->SetTextColor(C_MENU_TEXT_WHITE);
		}

		// Columns are aligned by padding, which is why this list uses the fixed pitch
		// font. Replies are indented under the message they answer.
		string cpdlcOutput = CPDLCMessage::ZuluTimeFormated(msg.timeParsed);
		cpdlcOutput += msg.isdlMessage ? "  |D/L" : "  ^U/L";
		cpdlcOutput += row.isReply ? "     " : "  ";
		if (msg.messageType == "telex") {
			cpdlcOutput += "FTXT: ";
		}
		if (msg.rawMessageContent.length() > static_cast<size_t>(CPDLC_ROW_TEXT_CHARS)) {
			cpdlcOutput += msg.rawMessageContent.substr(0, CPDLC_ROW_TEXT_CHARS);
			cpdlcOutput += "  >";
		}
		else {
			cpdlcOutput += msg.rawMessageContent;
		}

		RECT r = { winOrigin.x + 3, winOrigin.y + deltay, winOrigin.x + rowWidth, winOrigin.y + deltay + CPDLC_ROW_HEIGHT };
		CopyRect(&element.m_ListBoxRect, &r);
		m_dc->Rectangle(&r);
		r.left += 6;
		m_dc->DrawText(cpdlcOutput.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

		deltay += CPDLC_ROW_HEIGHT;
	}

	RECT totalListBox{ winOrigin.x + 4, winOrigin.y, winOrigin.x + rowWidth, winOrigin.y + deltay };
	m_dc->Draw3dRect(&totalListBox, C_MENU_GREY2, C_MENU_GREY4);

	// restore, then delete: DeleteObject fails on a still-selected object and leaks it
	m_dc->RestoreDC(sDC);
	DeleteObject(penGrey1);
	DeleteObject(penGrey2);
	DeleteObject(brushGrey1);
	DeleteObject(brushGrey2);
	DeleteObject(brushSelected);
}

void SListBox::RenderListBox(int firstElem, int numElem, int maxElements, POINT winOrigin) {
	int sDC = m_dc->SaveDC();

	m_dc->SelectObject(CFontHelper::Segoe14);
	m_dc->SetTextColor(RGB(230, 230, 230));

	HPEN targetPen = CreatePen(PS_SOLID, 1, C_MENU_GREY1);
	HBRUSH targetBrush = CreateSolidBrush(C_MENU_GREY1);
	HBRUSH tb2 = CreateSolidBrush(C_MENU_GREY4);

	m_dc->SelectObject(targetPen);
	m_dc->SelectObject(targetBrush);

	int deltay = 0;
	for (auto& element : listBox_) {
		m_dc->SelectObject(targetBrush);
		this->m_width = element.m_width;
		RECT r = { winOrigin.x + 16, winOrigin.y + deltay, winOrigin.x + m_width - 16, winOrigin.y + deltay + 20 };
		CopyRect(&element.m_ListBoxRect, &r);

		if (element.m_selected_) {
			m_dc->SetTextColor(C_MENU_GREY1);
			m_dc->SelectObject(tb2);
		}
		else {
			m_dc->SetTextColor(RGB(230, 230, 230));
		}

		m_dc->Rectangle(&r);
		r.left += 6;
		m_dc->DrawText(element.m_ListBoxElementText.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

		r.top += deltay;
		deltay += 20;
	}
	RECT totalListBox{ winOrigin.x + 16, winOrigin.y,  winOrigin.x + m_width - 16, winOrigin.y + static_cast<int>(listBox_.size() * 20) };
	m_dc->Draw3dRect(&totalListBox, C_MENU_GREY2, C_MENU_GREY4);
	this->m_width = totalListBox.right - totalListBox.left;

	// restore, then delete
	m_dc->RestoreDC(sDC);
	DeleteObject(targetPen);
	DeleteObject(targetBrush);
	DeleteObject(tb2);
}

void STextField::RenderTextField(CDC* m_dc, POINT origin) {
	int sDC = m_dc->SaveDC();

	m_dc->SelectObject(CFontHelper::Segoe14);
	m_dc->SetTextColor(RGB(230, 230, 230));

	HPEN targetPen = CreatePen(PS_SOLID, 1, C_MENU_GREY1);
	HBRUSH targetBrush = CreateSolidBrush(C_MENU_GREY1);
	HBRUSH tb2 = CreateSolidBrush(C_MENU_GREY4);

	m_dc->SelectObject(targetPen);
	m_dc->SelectObject(targetBrush);

	RECT r = { origin.x + m_location_.x, origin.y + m_location_.y, origin.x + m_width + m_location_.x, origin.y + m_height + m_location_.y };

	if (m_textfield_type == TEXTFIELD_CPDLC_MESSAGE) {
		// The downlink being answered: id, time, then the body wrapped over the box.
		m_dc->SelectObject(CFontHelper::EuroscopeFixed14);
		m_dc->Rectangle(&r);
		CopyRect(&m_textRect, &r);
		m_dc->Draw3dRect(&r, C_MENU_GREY2, C_MENU_GREY4);
		r.left += 5;

		if (!m_cpdlcmessage.rawMessageContent.empty()) {
			if (m_cpdlcmessage.messageID != -1) {
				// The real MIN range is 0-63. Hoppie allows higher, so keep the value we
				// were given for the wire and only roll it over for display.
				const string dispMsgID = to_string(m_cpdlcmessage.messageID % 64);
				m_dc->DrawText(dispMsgID.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
				m_dc->DrawText(dispMsgID.c_str(), &r, DT_LEFT | DT_SINGLELINE);
			}
			r.left += 22;

			const string zulu = CPDLCMessage::ZuluTimeFormated(m_cpdlcmessage.timeParsed);
			m_dc->DrawText(zulu.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
			m_dc->DrawText(zulu.c_str(), &r, DT_LEFT | DT_SINGLELINE);

			r.left += 50;
			r.right = origin.x + m_width + m_location_.x;
			r.bottom = origin.y + m_location_.y + m_height;
			m_dc->DrawText(m_cpdlcmessage.rawMessageContent.c_str(), &r, DT_LEFT | DT_WORDBREAK);
		}
	}
	else if (m_textfield_type == TEXTFIELD_CPDLC_PENDING_UPLINK) {
		// The uplink being composed. '@' is the wire format's field separator, so it is
		// stripped for display.
		m_dc->SelectObject(CFontHelper::EuroscopeFixed14);
		m_dc->Rectangle(&r);
		CopyRect(&m_textRect, &r);
		m_dc->Draw3dRect(&r, C_MENU_GREY2, C_MENU_GREY4);
		r.left += 8;
		r.right = origin.x + m_width + m_location_.x;
		r.bottom = origin.y + m_location_.y + m_height;

		string s = m_cpdlcmessage.rawMessageContent;
		s.erase(std::remove(s.begin(), s.end(), '@'), s.end());
		m_dc->DrawText(s.c_str(), &r, DT_LEFT | DT_WORDBREAK);
	}
	else {
		m_dc->Rectangle(&r);
		if (m_focused) {
			m_dc->Draw3dRect(&r, C_CPDLC_GREEN, C_CPDLC_GREEN);
		}
		else {
			m_dc->Draw3dRect(&r, C_MENU_GREY2, C_MENU_GREY4);
		}
		r.left += 6;
		m_dc->DrawText(m_text.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

		CopyRect(&m_textRect, &r);
	}

	// restore, then delete
	m_dc->RestoreDC(sDC);
	DeleteObject(targetPen);
	DeleteObject(targetBrush);
	DeleteObject(tb2);
}

void SWindowText::RenderText(CDC* m_dc, POINT origin) {
	int sDC = m_dc->SaveDC();

	m_dc->SelectObject(CFontHelper::Segoe14);
	m_dc->SetTextColor(C_MENU_TEXT_WHITE);

	// Zero sized to start with; DT_CALCRECT grows it to fit before the text is drawn.
	RECT r = { origin.x + location.x, origin.y + location.y, origin.x + location.x, origin.y + location.y };
	m_dc->DrawText(text.c_str(), &r, DT_LEFT | DT_SINGLELINE | DT_CALCRECT);
	m_dc->DrawText(text.c_str(), &r, DT_LEFT | DT_SINGLELINE);

	m_dc->RestoreDC(sDC);
}
