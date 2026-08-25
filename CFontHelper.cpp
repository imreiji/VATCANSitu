#include "pch.h"
#include "CFontHelper.h"

CFont CFontHelper::Euroscope14;
CFont CFontHelper::EuroscopeBold;
CFont CFontHelper::Euroscope16;
CFont CFontHelper::Segoe12;
CFont CFontHelper::Segoe14;

void CFontHelper::CreateFonts()
{
	LOGFONT lgfont;

	std::memset(&lgfont, 0, sizeof(LOGFONT));

	lgfont.lfHeight = 14;
	lgfont.lfWeight = 500;
	strcpy_s(lgfont.lfFaceName, _T("EuroScope"));
	CFontHelper::Euroscope14.CreateFontIndirect(&lgfont);

	lgfont.lfWeight = 1200;
	CFontHelper::EuroscopeBold.CreateFontIndirect(&lgfont);

	lgfont.lfWeight = 500;
	lgfont.lfHeight = 16;
	CFontHelper::Euroscope16.CreateFontIndirect(&lgfont);


	lgfont.lfWeight = 700;
	strcpy_s(lgfont.lfFaceName, _T("Segoe UI"));
	lgfont.lfHeight = 12;
	CFontHelper::Segoe12.CreateFontIndirect(&lgfont);

	lgfont.lfWeight = 500;
	strcpy_s(lgfont.lfFaceName, _T("Segoe UI"));
	lgfont.lfHeight = 14;
	CFontHelper::Segoe14.CreateFontIndirect(&lgfont);

}

void CFontHelper::DeleteFonts() {
	// These are MFC CFont objects, whose destructors already call DeleteObject when the
	// statics are torn down at DLL unload. Passing them to the Win32 DeleteObject here
	// freed each handle a second time - and by then the handle may have been reused.
	// Segoe12 and Segoe14 were never in the list at all, which is what made the
	// asymmetry visible. Use the MFC member, which also nulls the internal handle so the
	// destructor becomes a no-op.
	CFontHelper::Euroscope14.DeleteObject();
	CFontHelper::Euroscope16.DeleteObject();
	CFontHelper::EuroscopeBold.DeleteObject();
	CFontHelper::Segoe12.DeleteObject();
	CFontHelper::Segoe14.DeleteObject();
}
