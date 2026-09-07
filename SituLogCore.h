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

    enum class LogAction { NotOurs, Help, On, Off, Follow, FollowAll, Unfollow, Status };

    struct LogCommand
    {
        LogAction action = LogAction::NotOurs;
        std::string arg;
    };

    inline std::string ToUpper(std::string s)
    {
        for (char& c : s) { if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); } }
        return s;
    }

    inline std::vector<std::string> SplitWords(const std::string& text)
    {
        std::vector<std::string> words;
        std::string current;
        for (char c : text)
        {
            if (c == ' ' || c == '\t')
            {
                if (!current.empty()) { words.push_back(current); current.clear(); }
            }
            else { current += c; }
        }
        if (!current.empty()) { words.push_back(current); }
        return words;
    }

    // ".situ log <word>" -> an action. Exactly one word is accepted after the prefix; more
    // is a typo and gets the help text rather than a guess.
    inline LogCommand ParseLogCommand(const std::string& commandLine)
    {
        LogCommand command;

        const std::vector<std::string> words = SplitWords(commandLine);
        if (words.size() < 2) { return command; }
        if (ToUpper(words[0]) != ".SITU" || ToUpper(words[1]) != "LOG") { return command; }

        if (words.size() == 2) { command.action = LogAction::Help; return command; }
        if (words.size() > 3) { command.action = LogAction::Help; return command; }

        const std::string word = ToUpper(words[2]);
        if (word == "ON")          { command.action = LogAction::On; }
        else if (word == "OFF")    { command.action = LogAction::Off; }
        else if (word == "ALL")    { command.action = LogAction::FollowAll; }
        else if (word == "NONE")   { command.action = LogAction::Unfollow; }
        else if (word == "STATUS") { command.action = LogAction::Status; }
        else
        {
            command.action = LogAction::Follow;
            command.arg = word;
        }
        return command;
    }

    inline const char* const kLogPrefix = "SituDebug-";
    inline const char* const kLogSuffix = ".log";

    inline bool IsLogFileName(const std::string& name)
    {
        const std::string prefix = kLogPrefix;
        const std::string suffix = kLogSuffix;
        if (name.size() <= prefix.size() + suffix.size()) { return false; }
        if (name.compare(0, prefix.size(), prefix) != 0) { return false; }
        return name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    inline std::string LogFileName(const std::string& stamp)
    {
        return std::string(kLogPrefix) + stamp + kLogSuffix;
    }

    // Names sort lexically, and the stamp is zero-padded yyyymmdd-hhmmss, so lexical order
    // is time order. Everything but the newest `keep` is a victim, oldest first.
    inline std::vector<std::string> RotationVictims(std::vector<std::string> names, size_t keep)
    {
        std::vector<std::string> logs;
        for (const std::string& name : names)
        {
            if (IsLogFileName(name)) { logs.push_back(name); }
        }
        std::sort(logs.begin(), logs.end());

        if (logs.size() <= keep) { return {}; }
        logs.resize(logs.size() - keep);
        return logs;
    }
}
