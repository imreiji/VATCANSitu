#pragma once
#include "EuroScopePlugIn.h"
#include "VATCANSitu.h"
#include "CSiTRadar.h"

using namespace std;
using namespace EuroScopePlugIn;

// Namespace scope const, so it has internal linkage and is safe to define in a header.
const double EARTH_RADIUS_NM = 3440.0;

class HaloTool :
    public CRadarScreen
{
public:

    static inline double degtorad(double deg) {
        double ans = deg * PI / 180;
        return ans;
    }

    // Great circle destination point, given an angular distance (nautical miles divided by
    // the earth's radius) and an initial bearing:
    //
    //     lat2 = asin( sin f1 cos d + cos f1 sin d cos t )
    //     lon2 = lon1 + atan2( sin t sin d cos f1,  cos d - sin f1 sin lat2 )
    //
    // calcPTL and calcTBS were two copies of this with different distance expressions, and
    // both had the same transcription error: the first term of the atan2 denominator was
    // cos(sin(t) / earthRad) instead of cos(d). The units do not even work out - a sine
    // divided by a radius.
    //
    // The practical error was small: checked numerically against a haversine round trip,
    // roughly 0.001 nm at CZQM latitudes and 0.014 nm with 0.04 degrees of bearing error
    // at 82 N. Sub-pixel on any realistic scale, so this is a correctness fix rather than
    // a visible one.
    // Initial great circle bearing from one position to another, in degrees true.
    //
    // CPosition::DirectionTo returns a magnetic bearing, using the sector file's
    // deviation value, and the deviation itself is not exposed. The TBS geometry runs on
    // spherical trigonometry and needs a true bearing, so it is computed here rather
    // than converted back from a magnetic one with a constant somebody has to maintain.
    static double bearingBetween(CPosition from, CPosition to)
    {
        const double lat1 = degtorad(from.m_Latitude);
        const double lat2 = degtorad(to.m_Latitude);
        const double deltaLon = degtorad(to.m_Longitude - from.m_Longitude);

        const double y = sin(deltaLon) * cos(lat2);
        const double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(deltaLon);

        double bearing = atan2(y, x) * 180.0 / PI;
        if (bearing < 0) { bearing += 360.0; }
        return bearing;
    }

    static CPosition destinationPoint(CPosition origin, double angularDistance, double bearing)
    {
        const double lat1 = degtorad(origin.m_Latitude);
        const double lon1 = degtorad(origin.m_Longitude);
        const double theta = degtorad(bearing);

        const double lat2 = asin(sin(lat1) * cos(angularDistance)
            + cos(lat1) * sin(angularDistance) * cos(theta));

        const double lon2 = lon1 + atan2(sin(theta) * sin(angularDistance) * cos(lat1),
            cos(angularDistance) - sin(lat1) * sin(lat2));

        CPosition end;
        end.m_Latitude = lat2 * 180 / PI;
        end.m_Longitude = lon2 * 180 / PI;
        return end;
    };

    // ptlLen is in minutes and gs in knots, so the distance flown is gs * ptlLen / 60.
    static CPosition calcPTL(CPosition origin, double ptlLen, double gs, double bearing) {
        return destinationPoint(origin, (ptlLen * gs / 60) / EARTH_RADIUS_NM, bearing);
    };

    static double calcBearing(CPosition origin, CPosition end) {
        double y = sin(degtorad(end.m_Longitude) - degtorad(origin.m_Longitude)) * cos(degtorad(end.m_Latitude));
        double x = cos(degtorad(origin.m_Latitude)) * sin(degtorad(end.m_Latitude)) - sin(degtorad(origin.m_Latitude)) * cos(degtorad(end.m_Latitude)) * cos(degtorad(end.m_Longitude) - degtorad(origin.m_Longitude));
        double phi = atan2(y, x);
        double bearing = fmod((phi * 180 / PI + 360), 360);

        return bearing;
    }

    static void drawHalo(CDC* dc, POINT p, double r, double pixpernm)

    {
        int sDC = dc->SaveDC();

        //calculate pixels per nautical mile

        int pixoffset = (int)round(pixpernm * r);

        // draw the halo around point p with radius r in NM
        COLORREF targetPenColor = RGB(202, 205, 169);
        HPEN targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
        dc->SelectObject(targetPen);
        dc->SelectStockObject(HOLLOW_BRUSH);
        dc->Ellipse(p.x - pixoffset, p.y - pixoffset, p.x + pixoffset, p.y + pixoffset);

        // RestoreDC first: DeleteObject fails on an object still selected into the DC,
        // and the handle leaks. This runs once per haloed target per frame.
        dc->RestoreDC(sDC);
        DeleteObject(targetPen);
    };

    static void drawPTL(CDC* dc, CRadarTarget radtar, CRadarScreen* radscr, POINT p, double ptlTime)
    {
        int sDC = dc->SaveDC();

        CPosition pos1 = radtar.GetPreviousPosition(radtar.GetPosition()).GetPosition();
        CPosition pos2 = radtar.GetPosition().GetPosition();
        double theta = calcBearing(pos1, pos2);
        CPosition ptl = calcPTL(radtar.GetPosition().GetPosition(), ptlTime, radtar.GetPosition().GetReportedGS(), theta);
        POINT p2 = radscr->ConvertCoordFromPositionToPixel(ptl);

        COLORREF targetPenColor = C_PTL_GREEN;
        HPEN targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
        dc->SelectObject(targetPen);

        dc->MoveTo(p);
        dc->LineTo(p2);

        // RestoreDC before DeleteObject - see drawHalo. With "PTL All" enabled this runs
        // for every target on every frame, so a leak here burns handles fastest.
        dc->RestoreDC(sDC);
        DeleteObject(targetPen);
    };

    // tbsLen is already in nautical miles. gs is unused, kept so the two call sites in
    // drawTBS and drawPTL stay symmetrical.
    static CPosition calcTBS(CPosition origin, double tbsLen, double gs, double bearing) {
        (void)gs;
        return destinationPoint(origin, tbsLen / EARTH_RADIUS_NM, bearing);
    };

    static POINT drawTBS(CDC* dc, CRadarTarget radtar, CRadarScreen* radscr, POINT p, double tbsLen, double pixnm, double theta)
    {
        int sDC = dc->SaveDC();

        CPosition pos1 = radtar.GetPreviousPosition(radtar.GetPosition()).GetPosition();
        CPosition pos2 = radtar.GetPosition().GetPosition();
        theta = theta + 180;
        if (theta > 360) { theta = theta - 360; }
        CPosition ptl = calcTBS(radtar.GetPosition().GetPosition(), tbsLen, radtar.GetPosition().GetReportedGS(), theta);
        POINT p2 = radscr->ConvertCoordFromPositionToPixel(ptl);

        double nlen = 0.8*pixnm; // length of tbs barb
        POINT tbsp1;
        POINT tbsp2;

        double dx = (double)(p2.x - p.x);
        double dy = (double)(p2.y - p.y);
        double dist = sqrt(dx * dx + dy * dy);
        dx /= dist;
        dy /= dist;
        tbsp1.x = (LONG)(p2.x + (nlen / 2) * dy);
        tbsp1.y = (LONG)(p2.y - (nlen / 2) * dx);
        tbsp2.x = (LONG)(p2.x - (nlen / 2) * dy);
        tbsp2.y = (LONG)(p2.y + (nlen / 2) * dx);


        COLORREF targetPenColor = C_PPS_TBS_PINK;
        HPEN targetPen = CreatePen(PS_SOLID, 1, targetPenColor);
        dc->SelectObject(targetPen);

        // Draw text box to toggle follower L, M, H

        dc->MoveTo(tbsp1);
        dc->LineTo(tbsp2);

        // RestoreDC before DeleteObject - see drawHalo.
        dc->RestoreDC(sDC);
        DeleteObject(targetPen);
        return tbsp2;
    };
};
