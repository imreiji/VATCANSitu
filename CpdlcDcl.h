#pragma once

// Departure clearance templates, read from the [DCL] section of SituCPDLC.txt.
//
// These were two near-identical code branches selected by comparing the origin against
// "CYTZ" and "CYUL", plus a third for everywhere else. The three built the same string
// with one field's difference between them - Toronto ends CONTACT <unit> ON FREQ <freq>,
// Montreal ends NEXT FREQ <freq> ATIS <atis> - and about forty five lines of appending
// to say it.
//
// Format:
//
//     DCL:<adep>:<subtype>:<message type>:<response>:<text>
//
//   <adep>          comma separated departure ICAOs, or * for any
//   <subtype>       a name the caller asks for, or * for any
//   <message type>  cpdlc or telex
//   <response>      WU, AN, R, NE, or empty
//   <text>          the clearance, with <field> placeholders
//
// Rows are tried in file order and the first match wins, so a specific row must precede
// the wildcard that would also match it. That is TopSky's rule for the same directive.
//
// The text is the remainder after the fifth delimiter rather than a sixth field, so a
// colon inside a clearance cannot split it. That is the same mistake CpdlcPacket.h
// exists to avoid, and it is worth not making twice.
//
// This diverges from TopSky's DCL:Adep:Subtype:Text deliberately. Its placeholder names
// differ from ours anyway, so the rows are not portable between the two whatever we do,
// and putting the fixed fields ahead of the free text is what makes the free text safe.
//
// Depends only on ConfigFile.h and the standard library. See tests/CpdlcDclTests.cpp.

#include "ConfigFile.h"

#include <string>
#include <vector>

namespace SituCpdlcDcl
{
    struct Rule
    {
        std::string adep;             // as written, comma separated, or "*"
        std::string subtype;          // or "*"
        std::string messageType;      // cpdlc or telex
        std::string responseRequired; // WU, AN, R, NE, or empty
        std::string text;
    };

    struct Table
    {
        std::vector<Rule> rules;
        std::vector<int> skippedLines;
    };

    // One entry of a comma separated list, trimmed of surrounding spaces.
    inline bool ListContains(const std::string& list, const std::string& value)
    {
        size_t start = 0;
        while (start <= list.size())
        {
            size_t comma = list.find(',', start);
            if (comma == std::string::npos) { comma = list.size(); }

            size_t from = start;
            size_t to = comma;
            while (from < to && list[from] == ' ') { ++from; }
            while (to > from && list[to - 1] == ' ') { --to; }

            if (list.compare(from, to - from, value) == 0) { return true; }
            if (comma == list.size()) { break; }
            start = comma + 1;
        }
        return false;
    }

    inline Table Parse(const SituConfig::ParseResult& parsed)
    {
        Table table;

        for (const SituConfig::Record& record : parsed.records)
        {
            if (record.isAssignment || record.type != "DCL") { continue; }

            // adep, subtype, type, response, and at least the start of the text.
            if (record.fields.size() < 5)
            {
                table.skippedLines.push_back(record.line);
                continue;
            }

            Rule rule;
            rule.adep = record.fields[0];
            rule.subtype = record.fields[1];
            rule.messageType = record.fields[2];
            rule.responseRequired = record.fields[3];

            // The text is everything from the fifth field on, with the delimiters that
            // were inside it put back. Splitting gave them to us as separate fields.
            for (size_t i = 4; i < record.fields.size(); ++i)
            {
                if (i > 4) { rule.text += ":"; }
                rule.text += record.fields[i];
            }

            if (rule.adep.empty() || rule.subtype.empty() || rule.messageType.empty() || rule.text.empty())
            {
                table.skippedLines.push_back(record.line);
                continue;
            }

            table.rules.push_back(rule);
        }

        return table;
    }

    // First row matching both the departure airport and the subtype. Null when none does,
    // which the caller must treat as "no clearance to send" rather than sending an empty
    // one.
    inline const Rule* Select(const Table& table, const std::string& adep, const std::string& subtype)
    {
        for (const Rule& rule : table.rules)
        {
            if (rule.adep != "*" && !ListContains(rule.adep, adep)) { continue; }
            if (rule.subtype != "*" && rule.subtype != subtype) { continue; }
            return &rule;
        }
        return nullptr;
    }

    struct Field
    {
        std::string token;   // without the angle brackets
        std::string value;
    };

    // Substitutes <token> for each field given. A placeholder with no field supplied is
    // left as written rather than blanked, so a template naming something the caller did
    // not provide is visible in the message instead of quietly producing a gap.
    inline std::string Format(const std::string& text, const std::vector<Field>& fields)
    {
        std::string out;
        out.reserve(text.size());

        size_t i = 0;
        while (i < text.size())
        {
            if (text[i] != '<') { out.push_back(text[i]); ++i; continue; }

            const size_t close = text.find('>', i + 1);
            if (close == std::string::npos) { out.append(text.substr(i)); break; }

            const std::string token = text.substr(i + 1, close - i - 1);

            const Field* match = nullptr;
            for (const Field& field : fields)
            {
                if (field.token == token) { match = &field; break; }
            }

            if (match != nullptr) { out += match->value; }
            else                  { out += text.substr(i, close - i + 1); }

            i = close + 1;
        }
        return out;
    }
}
