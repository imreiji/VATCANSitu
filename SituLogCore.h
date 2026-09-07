#pragma once

// The pure half of the SituDebug log: line formatting, command parsing, rotation and the
// draw-decision snapshot. No I/O, no SDK, no Windows headers, so every rule here is
// testable without loading the plugin. The file sink and process state are in SituLog.h.
//
// See docs/superpowers/specs/2026-09-07-situ-debug-log-design.md and tests/SituLogTests.cpp.

#include <string>
#include <utility>
#include <vector>
#include <cstdio>
#include <algorithm>

namespace SituLog
{
    // key=value pairs for one line, in insertion order. Built inline at call sites:
    //   Fields().Add("callsign", cs).Add("squawk", sq)
    class Fields
    {
    public:
        Fields& Add(const std::string& key, const std::string& value)
        {
            items_.emplace_back(key, value);
            return *this;
        }
        Fields& Add(const std::string& key, const char* value)
        {
            return Add(key, std::string(value != nullptr ? value : ""));
        }
        Fields& Add(const std::string& key, bool value)
        {
            return Add(key, std::string(value ? "1" : "0"));
        }
        Fields& Add(const std::string& key, int value)
        {
            return Add(key, std::to_string(value));
        }
        Fields& Add(const std::string& key, double value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return Add(key, std::string(buffer));
        }

        const std::vector<std::pair<std::string, std::string>>& Items() const { return items_; }

    private:
        std::vector<std::pair<std::string, std::string>> items_;
    };

    // A value is quoted when a reader could otherwise mis-split it: a space ends a pair, an
    // equals sign starts a value, a quote opens one. Internal quotes are doubled, CSV style.
    inline std::string QuoteValue(const std::string& value)
    {
        bool needsQuotes = false;
        for (char c : value)
        {
            if (c == ' ' || c == '"' || c == '=') { needsQuotes = true; break; }
        }
        if (!needsQuotes) { return value; }

        std::string out;
        out.reserve(value.size() + 2);
        out += '"';
        for (char c : value)
        {
            if (c == '"') { out += '"'; }
            out += c;
        }
        out += '"';
        return out;
    }

    inline std::string Truncate(const std::string& text, size_t max)
    {
        if (text.size() <= max) { return text; }
        return text.substr(0, max) + "...";
    }

    inline std::string PadRight(const std::string& text, size_t width)
    {
        if (text.size() >= width) { return text; }
        return text + std::string(width - text.size(), ' ');
    }

    // HH:MM:SS.mmm CAT  SUBJECT     key=value ...
    // The caller supplies the time so this stays clock-free.
    inline std::string FormatLine(const std::string& time,
                                  const char* cat,
                                  const std::string& subject,
                                  const Fields& fields)
    {
        std::string line = time;
        line += ' ';
        line += PadRight(cat != nullptr ? cat : "", 4);
        line += ' ';
        line += PadRight(subject, 11);
        line += ' ';

        bool first = true;
        for (const auto& item : fields.Items())
        {
            if (!first) { line += ' '; }
            first = false;
            line += item.first;
            line += '=';
            line += QuoteValue(item.second);
        }
        return line;
    }
}
