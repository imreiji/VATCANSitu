#pragma once

// The canned replies and standing messages, from the [FREETEXT] section of
// SituCPDLC.txt.
//
//     FREETEXT:<reply type>:<text>
//
// Reply type is what the uplink asks the aircraft for:
//
//     WU      wilco or unable
//     AN      affirm or negative
//     R       roger
//     NE      no reply expected
//     UNICOM  a behaviour rather than a reply type - see below
//
// UNICOM is TopSky's, and it is worth knowing before copying a file across. It is not a
// reply attribute: the message is sent asking for wilco or unable, and on wilco TopSky
// closes every open dialogue, terminates the CPDLC connection and drops the track. We
// treat it as WU and do none of the rest, because silently dropping a track because a
// pilot answered a message is not a thing to inherit by accident.
//
// The window's reply buttons are labelled in title case - "Unable", "Roger" - while the
// wire text is upper case, so lookup is case insensitive.
//
// Depends only on ConfigFile.h and the standard library. See tests/CpdlcFreetextTests.cpp.

#include "ConfigFile.h"

#include <string>
#include <vector>

namespace SituCpdlcFreetext
{
    struct Entry
    {
        std::string replyType;   // WU, AN, R, NE - UNICOM is normalised to WU
        std::string text;

        // True when the file said UNICOM. Recorded so the behaviour can be implemented
        // deliberately later rather than being lost, but nothing acts on it today.
        bool terminatesService = false;
    };

    struct Table
    {
        std::vector<Entry> entries;
        std::vector<int> skippedLines;
    };

    inline std::string Upper(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) { out.push_back((c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c); }
        return out;
    }

    inline bool IsReplyType(const std::string& value)
    {
        return value == "WU" || value == "AN" || value == "R" || value == "NE" || value == "UNICOM";
    }

    inline Table Parse(const SituConfig::ParseResult& parsed)
    {
        Table table;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (record.isAssignment || record.type != "FREETEXT") { continue; }

            if (record.fields.size() < 2)
            {
                table.skippedLines.push_back(record.line);
                continue;
            }

            const std::string type = Upper(record.fields[0]);
            if (!IsReplyType(type))
            {
                table.skippedLines.push_back(record.line);
                continue;
            }

            // The text is everything from the second field on, with the delimiters that
            // were inside it put back - the same reason CpdlcDcl and CpdlcPacket do it:
            // a colon in a message must not split the message.
            std::string text;
            for (size_t i = 1; i < record.fields.size(); ++i)
            {
                if (i > 1) { text += ":"; }
                text += record.fields[i];
            }

            if (text.empty())
            {
                table.skippedLines.push_back(record.line);
                continue;
            }

            Entry entry;
            entry.terminatesService = (type == "UNICOM");
            entry.replyType = entry.terminatesService ? "WU" : type;
            entry.text = text;
            table.entries.push_back(entry);
        }

        return table;
    }

    // Exact match on the message text, case insensitively. Null when the file has no
    // such message, which the caller should treat as "this button has nothing behind it"
    // rather than inventing a reply type.
    inline const Entry* Find(const Table& table, const std::string& text)
    {
        const std::string wanted = Upper(text);
        for (const Entry& entry : table.entries)
        {
            if (Upper(entry.text) == wanted) { return &entry; }
        }
        return nullptr;
    }
}
