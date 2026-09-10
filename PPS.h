#pragma once

#include "EuroScopePlugIn.h"
#include "CSiTRadar.h"

using namespace std;
using namespace EuroScopePlugIn;

class CPPS :
    public EuroScopePlugIn::CRadarScreen
{

public:
    // shapeOut, when given, receives the name of the symbol actually drawn - the one left
	// on screen after any fall through. Used by the SituDebug DRAW line; nothing about the
	// drawing depends on it.
	static RECT DrawPPS(CDC* dc, BOOL isCorrelated, BOOL isVFR, BOOL isADSB, BOOL isRVSM, int radFlag, COLORREF ppsColor, string squawk, POINT p, const char** shapeOut = nullptr)
	{

		int sDC = dc->SaveDC();

		const char* shape = "NONE";

		// Add the screenobject
		RECT prect;
		prect.left = p.x - 5;
		prect.top = p.y - 5;
		prect.right = p.x + 5;
		prect.bottom = p.y + 5;

		HPEN targetPen;

		/// DRAWING SYMBOLS ///

		targetPen = CreatePen(PS_SOLID, 1, ppsColor);
		dc->SelectObject(targetPen);
		dc->SelectStockObject(NULL_BRUSH);

		switch (radFlag) {
		case 0:
			// Code for radFlag equals 0
			break;

		case 1:
			if (isCorrelated) {
				shape = "TRIANGLE";
				dc->SelectStockObject(NULL_BRUSH);

				POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Yellow Triangle
				dc->Polygon(vertices, 3);
			}
			else {
				shape = "Y";
				dc->MoveTo(p.x, p.y + 4);	// Magenta Y 
				dc->LineTo(p.x, p.y);
				dc->LineTo(p.x - 4, p.y - 4);
				dc->MoveTo(p.x, p.y);
				dc->LineTo(p.x + 4, p.y - 4);
			}
			break;

		case 2:												// Code for radFlag equals 2 = SSR ONLY
		case 3: {

			if (!strcmp(squawk.c_str(), "7600") || !strcmp(squawk.c_str(), "7700")) {
				// Nested save/restore so the brush is deselected before it is deleted.
				// The outer SaveDC is only restored on the way out of the function, well
				// after this DeleteObject would have silently failed and leaked.
				int sDCEmerg = dc->SaveDC();

				shape = "TRIANGLE-FILLED";

				HBRUSH targetBrush = CreateSolidBrush(ppsColor);
				dc->SelectObject(targetBrush);

				POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Red triangle
				dc->Polygon(vertices, 3);

				dc->RestoreDC(sDCEmerg);
				DeleteObject(targetBrush);

				break;
			}

			if (!strcmp(squawk.c_str(), "1200") && !isCorrelated && radFlag != 1) { // Eventually change to block squawk codes

				shape = "TRIANGLE";
				dc->SelectStockObject(NULL_BRUSH);

				POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Yellow Triangle
				dc->Polygon(vertices, 3);

				break;
			}

			if (isCorrelated && !isVFR && !isRVSM) {		// Code for radFlag equals 3 = SSR+PSR
				shape = "HEXAGON";
				dc->MoveTo(p.x - 4, p.y - 2);
				dc->LineTo(p.x - 4, p.y + 2);
				dc->LineTo(p.x, p.y + 5);
				dc->LineTo(p.x + 4, p.y + 2);
				dc->LineTo(p.x + 4, p.y - 2);
				dc->LineTo(p.x, p.y - 5);
				dc->LineTo(p.x - 4, p.y - 2);
			}
			if (isCorrelated && !isVFR && isRVSM) {
				shape = "DIAMOND";
				dc->MoveTo(p.x, p.y - 5);
				dc->LineTo(p.x + 5, p.y);
				dc->LineTo(p.x, p.y + 5);
				dc->LineTo(p.x - 5, p.y);
				dc->LineTo(p.x, p.y - 5);

				dc->MoveTo(p.x, p.y - 5);
				dc->LineTo(p.x, p.y + 5);

			}
			if (isCorrelated && isVFR) {
				shape = "CIRCLE-CHECK";
				dc->SelectStockObject(NULL_BRUSH);

				// draw the shape
				dc->Ellipse(p.x - 4, p.y - 4, p.x + 6, p.y + 6);

				dc->MoveTo(p.x - 3, p.y - 2);
				dc->LineTo(p.x + 1, p.y + 4);
				dc->LineTo(p.x + 4, p.y - 2);
			}
			if (!isCorrelated) {

				shape = "ASTERISK";
				dc->MoveTo(p.x - 4, p.y - 4);
				dc->LineTo(p.x + 5, p.y + 5);
				dc->MoveTo(p.x, p.y - 5);
				dc->LineTo(p.x, p.y + 6);
				dc->MoveTo(p.x + 4, p.y - 4);
				dc->LineTo(p.x - 5, p.y + 5);
				dc->MoveTo(p.x - 5, p.y);
				dc->LineTo(p.x + 6, p.y);

			}
			break;
		}

		case 4: {
			
			if(!isADSB) { break; } 

			// An uncorrelated aircraft on 1200 is a VFR conspicuity return, and that is
			// what the symbol has to say - the ADS-B square would claim an identity
			// nothing has established. radFlag 2 and 3 already do this; ADS-B did not,
			// so an ADS-B equipped VFR aircraft squawking 1200 drew as a datalinked
			// target instead of as an unknown VFR one.
			if (!strcmp(squawk.c_str(), "1200") && !isCorrelated) {

				shape = "TRIANGLE";
				dc->SelectStockObject(NULL_BRUSH);

				POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Yellow Triangle
				dc->Polygon(vertices, 3);

				break;
			}
			
			else {

				if(isCorrelated) {
				shape = "SQUARE";
				dc->MoveTo(p.x - 4, p.y - 4);
				dc->LineTo(p.x + 4, p.y - 4);
				dc->LineTo(p.x + 4, p.y + 4);
				dc->LineTo(p.x - 4, p.y + 4);
				dc->LineTo(p.x - 4, p.y - 4);
	
				// RVSM ADSB symbol
					if (isRVSM) {
						shape = "SQUARE-BAR";
						dc->MoveTo(p.x, p.y - 4);
						dc->LineTo(p.x, p.y + 4);
					}
				}

				else {
					// ADSB non-correlated synmbol
		
					shape = "SQUARE-RAYS";
					dc->MoveTo(p.x - 4, p.y - 4);
					dc->LineTo(p.x + 4, p.y - 4);
					dc->LineTo(p.x + 4, p.y + 4);
					dc->LineTo(p.x - 4, p.y + 4);
					dc->LineTo(p.x - 4, p.y - 4);
					dc->MoveTo(p.x - 6, p.y - 6);
					dc->LineTo(p.x - 1, p.y - 1);
					dc->MoveTo(p.x + 6, p.y - 6);
					dc->LineTo(p.x + 1, p.y - 1);
					dc->MoveTo(p.x - 6, p.y + 6);
					dc->LineTo(p.x - 1, p.y + 1);
					dc->MoveTo(p.x + 6, p.y + 6);
					dc->LineTo(p.x + 1, p.y + 1);
		
					dc->MoveTo(p.x, p.y - 6);
					dc->LineTo(p.x, p.y - 1);
		
					dc->MoveTo(p.x + 6, p.y);
					dc->LineTo(p.x + 1, p.y);
		
					dc->MoveTo(p.x - 6, p.y);
					dc->LineTo(p.x - 1, p.y);
		
					dc->MoveTo(p.x, p.y + 6);
					dc->LineTo(p.x, p.y + 1);
	
				}
			}
		}
		case 5:
		case 6:
		case 7: {
			// Code for radFlag equals 4 = MODE C = ADSB

			// Same rule as case 4 above: 1200 and uncorrelated is a VFR conspicuity
			// return whatever the equipment says.
			if (isADSB && !strcmp(squawk.c_str(), "1200") && !isCorrelated) {

				shape = "TRIANGLE";
				dc->SelectStockObject(NULL_BRUSH);

				POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Yellow Triangle
				dc->Polygon(vertices, 3);

				break;
			}

			if (isADSB) {
				shape = "SQUARE";
				dc->MoveTo(p.x - 4, p.y - 4);
				dc->LineTo(p.x + 4, p.y - 4);
				dc->LineTo(p.x + 4, p.y + 4);
				dc->LineTo(p.x - 4, p.y + 4);
				dc->LineTo(p.x - 4, p.y - 4);

				// RVSM ADSB symbol
				if (isRVSM) {
					shape = "SQUARE-BAR";
					dc->MoveTo(p.x, p.y - 4);
					dc->LineTo(p.x, p.y + 4);
				}

				// ADSB non-correlated

				if (!isCorrelated) {

					// ADSB non-correlated synmbol

					shape = "SQUARE-RAYS";
					dc->MoveTo(p.x - 4, p.y - 4);
					dc->LineTo(p.x + 4, p.y - 4);
					dc->LineTo(p.x + 4, p.y + 4);
					dc->LineTo(p.x - 4, p.y + 4);
					dc->LineTo(p.x - 4, p.y - 4);
					dc->MoveTo(p.x - 6, p.y - 6);
					dc->LineTo(p.x - 1, p.y - 1);
					dc->MoveTo(p.x + 6, p.y - 6);
					dc->LineTo(p.x + 1, p.y - 1);
					dc->MoveTo(p.x - 6, p.y + 6);
					dc->LineTo(p.x - 1, p.y + 1);
					dc->MoveTo(p.x + 6, p.y + 6);
					dc->LineTo(p.x + 1, p.y + 1);

					dc->MoveTo(p.x, p.y - 6);
					dc->LineTo(p.x, p.y - 1);

					dc->MoveTo(p.x + 6, p.y);
					dc->LineTo(p.x + 1, p.y);

					dc->MoveTo(p.x - 6, p.y);
					dc->LineTo(p.x - 1, p.y);

					dc->MoveTo(p.x, p.y + 6);
					dc->LineTo(p.x, p.y + 1);

				}

			}
			else {
				// If not ADSB by equipment, treat like a RADFLAG 2 or 3 - including the
				// 1200 rule. This branch copied the shapes from the Mode A/C case but not
				// the test above them, so a non-ADS-B aircraft on 1200 under Mode S
				// coverage drew the uncorrelated asterisk instead of the VFR triangle.
				// Mode S coverage is what most sector files declare, so that was the
				// common case, not the exception.
				if (!strcmp(squawk.c_str(), "1200") && !isCorrelated) {

					shape = "TRIANGLE";
					dc->SelectStockObject(NULL_BRUSH);

					POINT vertices[] = { { p.x - 4, p.y + 4 } , { p.x, p.y - 4 } , { p.x + 4,p.y + 4 } }; // Yellow Triangle
					dc->Polygon(vertices, 3);

					break;
				}

				if (isCorrelated && !isVFR && !isRVSM) {		// Code for radFlag equals 3 = SSR+PSR
					shape = "HEXAGON";
					dc->MoveTo(p.x - 4, p.y - 2);
					dc->LineTo(p.x - 4, p.y + 2);
					dc->LineTo(p.x, p.y + 5);
					dc->LineTo(p.x + 4, p.y + 2);
					dc->LineTo(p.x + 4, p.y - 2);
					dc->LineTo(p.x, p.y - 5);
					dc->LineTo(p.x - 4, p.y - 2);
				}
				if (isCorrelated && !isVFR && isRVSM) {
					shape = "DIAMOND";
					dc->MoveTo(p.x, p.y - 5);
					dc->LineTo(p.x + 5, p.y);
					dc->LineTo(p.x, p.y + 5);
					dc->LineTo(p.x - 5, p.y);
					dc->LineTo(p.x, p.y - 5);

					dc->MoveTo(p.x, p.y - 5);
					dc->LineTo(p.x, p.y + 5);

				}
				if (isCorrelated && isVFR) {
					shape = "CIRCLE-CHECK";
					dc->SelectStockObject(NULL_BRUSH);

					// draw the shape
					dc->Ellipse(p.x - 4, p.y - 4, p.x + 6, p.y + 6);

					dc->MoveTo(p.x - 3, p.y - 2);
					dc->LineTo(p.x + 1, p.y + 4);
					dc->LineTo(p.x + 4, p.y - 2);
				}

				if (!isCorrelated) {

					shape = "ASTERISK";
					dc->MoveTo(p.x - 4, p.y - 4);
					dc->LineTo(p.x + 5, p.y + 5);
					dc->MoveTo(p.x, p.y - 5);
					dc->LineTo(p.x, p.y + 6);
					dc->MoveTo(p.x + 4, p.y - 4);
					dc->LineTo(p.x - 5, p.y + 5);
					dc->MoveTo(p.x - 5, p.y);
					dc->LineTo(p.x + 6, p.y);

				}
			}

			break;
		}
		}

		// restore, then delete - this runs for every radar target every frame
		dc->RestoreDC(sDC);
		DeleteObject(targetPen);
		if (shapeOut != nullptr) { *shapeOut = shape; }
		return prect;
	};

};

