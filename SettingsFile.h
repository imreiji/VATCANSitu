#pragma once

// The plugin's own settings, in the same plain text format as SituCPDLC.txt and
// SituTBS.txt.
//
// This replaces settings.json, which had a hazard in it worth naming. The file was read
// with nlohmann's json::parse inside a try block catching std::ifstream::failure - and
// json::parse_error does not derive from that. So one malformed character threw an
// uncaught exception out of the CSiTRadar constructor, which EuroScope calls from
// OnRadarScreenCreated. An exception leaving an SDK callback unwinds through stack
// frames built by a different compiler, in a separately linked binary. A missing key did
// the same thing by a different route: j["wxlat"] returns null, and assigning null to a
// std::string throws json::type_error.
//
// JSON is also all or nothing. One bad value lost every setting in the file. A line
// based format loses the line, keeps the rest, and can say which line it dropped.
//
// Credentials are deliberately not here. See LocalSettings, which belongs in a file that
// is never shipped with a sector package.
//
// Depends only on ConfigFile.h and the standard library. See tests/SettingsFileTests.cpp.

#include "ConfigFile.h"

#include <string>
#include <vector>

namespace SituSettings
{
    // Seven, matching the controller remark buttons.
    const size_t kControllerRemarkCount = 7;

    struct Point2
    {
        int x = 0;
        int y = 0;
    };

    struct Settings
    {
        // Weather radar tile centre, kept as text because that is what goes into the
        // RainViewer URL and round tripping it through a double would change it.
        std::string wxLat = "0.0";
        std::string wxLong = "0.0";

        // Offsets from the top left of the radar area, not screen coordinates.
        Point2 atisListOffset{ 500, 84 };
        Point2 offScreenListOffset{ 0, 500 };
        Point2 messageListOffset{ 800, 84 };

        int historyDots = 4;
        bool bigACID = true;
        bool wxAll = false;
        bool filterBypassAll = false;
        bool extAltToggle = false;

        std::string prefSFI = "ABCDEFGHIJ";
        std::vector<std::string> controllerRemarks{ kControllerRemarkCount, std::string() };

        // Where CPDLC is sent. Configurable so the plugin can be pointed at a local
        // distributor - a service holding one Hoppie connection and fanning out - without
        // touching transport code. TopSky exposes the same seam as CPDLC_Server and
        // CPDLC_Page, so a distributor speaking Hoppie's protocol is a settings change
        // for both plugins rather than a code change for either.
        std::string cpdlcServer = "https://www.hoppie.nl/acars/system/connect.html";

        std::vector<int> skippedLines;
    };

    // Per controller, and never shipped. TopSky draws the same line with
    // TopSkySettingsLocal.txt, loaded only on sign in, for the same reason: a logon code
    // in a file that travels with a sector package is a credential handed to everyone
    // who installs it.
    struct LocalSettings
    {
        std::string hoppieCode;
        std::string hoppieICAO;

        std::vector<int> skippedLines;
    };

    inline bool ParseBool(const std::string& value, bool& out)
    {
        if (value == "1" || value == "true" || value == "TRUE" || value == "yes") { out = true; return true; }
        if (value == "0" || value == "false" || value == "FALSE" || value == "no") { out = false; return true; }
        return false;
    }

    inline bool ParseInt(const std::string& value, int& out)
    {
        if (value.empty()) { return false; }

        size_t index = 0;
        bool negative = false;
        if (value[0] == '-' || value[0] == '+') { negative = (value[0] == '-'); index = 1; }
        if (index >= value.size()) { return false; }

        long long total = 0;
        for (; index < value.size(); ++index)
        {
            if (value[index] < '0' || value[index] > '9') { return false; }
            total = total * 10 + (value[index] - '0');
            if (total > 1000000) { return false; }
        }
        out = static_cast<int>(negative ? -total : total);
        return true;
    }

    // "500,84". Both halves must parse or the whole value is refused, so a half applied
    // position cannot put a list somewhere neither value asked for.
    inline bool ParsePoint(const std::string& value, Point2& out)
    {
        const size_t comma = value.find(',');
        if (comma == std::string::npos) { return false; }

        Point2 parsed;
        if (!ParseInt(value.substr(0, comma), parsed.x)) { return false; }
        if (!ParseInt(value.substr(comma + 1), parsed.y)) { return false; }

        out = parsed;
        return true;
    }

    inline std::string FormatPoint(const Point2& point)
    {
        return std::to_string(point.x) + "," + std::to_string(point.y);
    }

    // CtrlRemark1 through CtrlRemark7. Returns 0 when the key is not one of them.
    inline size_t ControllerRemarkIndex(const std::string& key)
    {
        const std::string prefix = "CtrlRemark";
        if (key.size() != prefix.size() + 1) { return 0; }
        if (key.compare(0, prefix.size(), prefix) != 0) { return 0; }

        const char digit = key[prefix.size()];
        if (digit < '1' || digit > '7') { return 0; }
        return static_cast<size_t>(digit - '0');
    }

    inline Settings Parse(const SituConfig::ParseResult& parsed)
    {
        Settings settings;
        settings.skippedLines = parsed.skippedLines;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (!record.isAssignment) { settings.skippedLines.push_back(record.line); continue; }

            const std::string& key = record.key;
            const std::string& value = record.value;
            bool handled = true;

            if (key == "WxLat")                     { settings.wxLat = value; }
            else if (key == "WxLong")               { settings.wxLong = value; }
            else if (key == "PrefSFI")              { settings.prefSFI = value; }
            else if (key == "CpdlcServer")          { settings.cpdlcServer = value; }
            else if (key == "AtisListOffset")       { handled = ParsePoint(value, settings.atisListOffset); }
            else if (key == "OffScreenListOffset")  { handled = ParsePoint(value, settings.offScreenListOffset); }
            else if (key == "MessageListOffset")    { handled = ParsePoint(value, settings.messageListOffset); }
            else if (key == "HistoryDots")          { handled = ParseInt(value, settings.historyDots); }
            else if (key == "BigACID")              { handled = ParseBool(value, settings.bigACID); }
            else if (key == "WxAll")                { handled = ParseBool(value, settings.wxAll); }
            else if (key == "FilterBypassAll")      { handled = ParseBool(value, settings.filterBypassAll); }
            else if (key == "ExtAltToggle")         { handled = ParseBool(value, settings.extAltToggle); }
            else
            {
                const size_t remark = ControllerRemarkIndex(key);
                if (remark != 0) { settings.controllerRemarks[remark - 1] = value; }
                else             { handled = false; }
            }

            if (!handled) { settings.skippedLines.push_back(record.line); }
        }

        return settings;
    }

    inline LocalSettings ParseLocal(const SituConfig::ParseResult& parsed)
    {
        LocalSettings local;
        local.skippedLines = parsed.skippedLines;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (!record.isAssignment) { local.skippedLines.push_back(record.line); continue; }

            if (record.key == "HoppieCode")      { local.hoppieCode = record.value; }
            else if (record.key == "HoppieICAO") { local.hoppieICAO = record.value; }
            else                                 { local.skippedLines.push_back(record.line); }
        }

        return local;
    }

    inline std::string Serialize(const Settings& settings)
    {
        std::string out;
        out += "// VATCANSitu settings. Written on exit; edit freely while EuroScope is closed.\n";
        out += "//\n";
        out += "// Credentials are not here. The Hoppie logon code lives in SituLocal.txt, which\n";
        out += "// is per controller and should not be shipped with a sector package.\n";
        out += "\n";
        out += "// Weather radar tile centre, in decimal degrees.\n";
        out += "WxLat=" + settings.wxLat + "\n";
        out += "WxLong=" + settings.wxLong + "\n";
        out += "\n";
        out += "// Where CPDLC is sent. Point this at a local distributor to share one Hoppie\n";
        out += "// connection across a sector rather than holding one per controller.\n";
        out += "CpdlcServer=" + settings.cpdlcServer + "\n";
        out += "\n";
        out += "// List positions, as x,y offsets from the top left of the radar area - not screen\n";
        out += "// coordinates, so they follow the header strip when the window moves or resizes.\n";
        out += "AtisListOffset=" + FormatPoint(settings.atisListOffset) + "\n";
        out += "OffScreenListOffset=" + FormatPoint(settings.offScreenListOffset) + "\n";
        out += "MessageListOffset=" + FormatPoint(settings.messageListOffset) + "\n";
        out += "\n";
        out += "// Display toggles.\n";
        out += "HistoryDots=" + std::to_string(settings.historyDots) + "\n";
        out += "BigACID=" + std::string(settings.bigACID ? "1" : "0") + "\n";
        out += "WxAll=" + std::string(settings.wxAll ? "1" : "0") + "\n";
        out += "FilterBypassAll=" + std::string(settings.filterBypassAll ? "1" : "0") + "\n";
        out += "ExtAltToggle=" + std::string(settings.extAltToggle ? "1" : "0") + "\n";
        out += "\n";
        out += "// Preferred order for the SFI picker.\n";
        out += "PrefSFI=" + settings.prefSFI + "\n";
        out += "\n";
        out += "// Controller remark presets, one per button.\n";
        for (size_t i = 0; i < kControllerRemarkCount; ++i)
        {
            const std::string value = (i < settings.controllerRemarks.size())
                ? settings.controllerRemarks[i]
                : std::string();
            out += "CtrlRemark" + std::to_string(i + 1) + "=" + value + "\n";
        }
        return out;
    }

    inline std::string SerializeLocal(const LocalSettings& local)
    {
        std::string out;
        out += "// VATCANSitu, per controller. The logon code below is a personal credential:\n";
        out += "// do not commit this file, and do not ship it with a sector package.\n";
        out += "//\n";
        out += "// The code expires after 120 days of inactivity. Request one at hoppie.nl.\n";
        out += "\n";
        out += "HoppieCode=" + local.hoppieCode + "\n";
        out += "HoppieICAO=" + local.hoppieICAO + "\n";
        return out;
    }
}
