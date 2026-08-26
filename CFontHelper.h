// Make fonts, brushes and pens once, to not need to do them over and over again
#include <afxwin.h>
#include <cstring>

#pragma once
class CFontHelper {
public:
	static CFont Euroscope14;
	static CFont EuroscopeBold;
	static CFont Euroscope16;
	// Fixed pitch, for the CPDLC message list and message fields, whose columns are
	// aligned by padding the string rather than by measuring it.
	static CFont EuroscopeFixed14;
	static CFont Segoe12;
	static CFont Segoe14;

	static void CreateFonts(); 
	static void DeleteFonts();
};