#pragma once

// One-time migration from settings.json.
//
// This is the only place nlohmann::json is still used for settings, it runs once on the
// first load after upgrading, and everything it reads is written straight back out as
// settings.txt and SituLocal.txt. Nothing calls it again afterwards.
//
// Every read here is guarded. The code this replaces was not: j["wxlat"] on a file
// without that key returns null, assigning null to a std::string throws
// json::type_error, and the surrounding catch was for std::ifstream::failure - a type
// neither that nor json::parse_error derives from. So a settings file that was merely
// old, never mind corrupt, threw out of the CSiTRadar constructor and unwound into
// EuroScope through a module boundary.
//
// Two list position key spellings are handled. Positions were once absolute screen
// coordinates under atisList/offScreenList/messageList, and are now offsets from the
// radar area origin under the same names with Offset appended. An absolute value read
// as an offset would move every list by the radar area origin, so the two are kept
// apart and the old form is converted on the first draw.

#include "SettingsFile.h"
#include "json.hpp"

#include <string>

namespace SituLegacy
{
    inline std::string StringOr(const nlohmann::json& j, const char* key, const std::string& fallback)
    {
        if (!j.contains(key) || !j[key].is_string()) { return fallback; }
        return j[key].get<std::string>();
    }

    inline int IntOr(const nlohmann::json& j, const char* key, int fallback)
    {
        if (!j.contains(key) || !j[key].is_number_integer()) { return fallback; }
        return j[key].get<int>();
    }

    inline bool BoolOr(const nlohmann::json& j, const char* key, bool fallback)
    {
        if (!j.contains(key) || !j[key].is_boolean()) { return fallback; }
        return j[key].get<bool>();
    }

    // Reads an {x, y} object under either key spelling. Returns false when neither is
    // present or usable, leaving the caller's default in place.
    inline bool PointOr(const nlohmann::json& j, const char* key, SituSettings::Point2& out)
    {
        if (!j.contains(key) || !j[key].is_object()) { return false; }

        const nlohmann::json& point = j[key];
        if (!point.contains("x") || !point.contains("y")) { return false; }
        if (!point["x"].is_number_integer() || !point["y"].is_number_integer()) { return false; }

        out.x = point["x"].get<int>();
        out.y = point["y"].get<int>();
        return true;
    }

    // Throws only if the document itself will not parse, which the caller catches by
    // std::exception. Everything past that point degrades to a default.
    inline SituSettings::Settings SettingsFromJson(const std::string& text,
                                                   SituSettings::LocalSettings& outLocal)
    {
        SituSettings::Settings settings;

        const nlohmann::json j = nlohmann::json::parse(text);
        if (!j.is_object()) { return settings; }

        settings.wxLat = StringOr(j, "wxlat", settings.wxLat);
        settings.wxLong = StringOr(j, "wxlong", settings.wxLong);
        settings.prefSFI = StringOr(j, "prefSFI", settings.prefSFI);

        // Prefer the offset spelling; fall back to the absolute one. A file old enough to
        // carry only the absolute form is rare by now and its positions were, at worst,
        // the defaults.
        if (!PointOr(j, "atisListOffset", settings.atisListOffset)) {
            PointOr(j, "atisList", settings.atisListOffset);
        }
        if (!PointOr(j, "offScreenListOffset", settings.offScreenListOffset)) {
            PointOr(j, "offScreenList", settings.offScreenListOffset);
        }
        if (!PointOr(j, "messageListOffset", settings.messageListOffset)) {
            PointOr(j, "messageList", settings.messageListOffset);
        }

        if (j.contains("menuState") && j["menuState"].is_object()) {
            const nlohmann::json& menu = j["menuState"];
            settings.historyDots = IntOr(menu, "numHistoryDots", settings.historyDots);
            settings.bigACID = BoolOr(menu, "bigACID", settings.bigACID);
            settings.wxAll = BoolOr(menu, "wxAll", settings.wxAll);
            settings.filterBypassAll = BoolOr(menu, "filterBypassAll", settings.filterBypassAll);
            settings.extAltToggle = BoolOr(menu, "extAltToggle", settings.extAltToggle);
        }

        if (j.contains("ctrlRemarks") && j["ctrlRemarks"].is_array()) {
            const nlohmann::json& remarks = j["ctrlRemarks"];
            for (size_t i = 0; i < settings.controllerRemarks.size() && i < remarks.size(); i++) {
                if (remarks[i].is_string()) {
                    settings.controllerRemarks[i] = remarks[i].get<std::string>();
                }
            }
        }

        // The credential moves out of the shared file and into the per controller one.
        outLocal.hoppieCode = StringOr(j, "hoppieCode", outLocal.hoppieCode);
        outLocal.hoppieICAO = StringOr(j, "hoppieICAO", outLocal.hoppieICAO);

        return settings;
    }
}
