#pragma once

// Tokeniser for the plain text config files, in the format TopSky uses.
//
// Matching that format exactly is deliberate. Controllers running CZQM already have
// TopSkyCPDLC.txt installed, with an agreed station list in it that took coordination
// between facilities to produce. If our file reads the same syntax, that list can be
// copied across rather than retyped, and stays correct when it is updated upstream.
//
// The syntax is shared by both files:
//
//   // a whole line comment, and the way disabled lines are kept in TopSky's own files
//   Key=Value
//   TYPE:field:field:field
//   [Section]
//
// The two files agree on this much and disagree on what a [Section] means - grouping in
// one, conditional inclusion by callsign in the other - so this produces records and
// leaves the meaning to the caller. One tokeniser, two interpreters.
//
// Depends on nothing but the standard library, so it is tested without MFC or the
// EuroScope SDK. See tests/ConfigFileTests.cpp.

#include <string>
#include <vector>

namespace SituConfig
{
    struct Record
    {
        // 1-based, for reporting which line was skipped.
        int line = 0;

        // The section this record appeared under. Empty for records above the first
        // header - TopSky's own CPDLC file opens with a Key=Value line before any
        // section, so that is a normal case rather than a malformed one.
        std::string section;

        // Set for Key=Value records.
        bool isAssignment = false;
        std::string key;
        std::string value;

        // Set for TYPE:field:field records. type is the part before the first colon.
        std::string type;
        std::vector<std::string> fields;
    };

    struct ParseResult
    {
        std::vector<Record> records;

        // Lines that were not blank, not a comment, and matched no record form. These
        // are reported rather than dropped: on a platform whose failure mode is silence,
        // counting what was skipped is what turns a silent misconfiguration into an
        // ordinary bug report.
        std::vector<int> skippedLines;
    };

    inline bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    // Leading and trailing whitespace, used only to decide whether a line is blank, a
    // comment or a section header. Field and value text is never trimmed: two of the
    // FREETEXT lines in the live CZQM file carry a deliberate trailing space, and a
    // parser that trims silently changes the message that goes on the wire.
    inline std::string Trim(const std::string& s)
    {
        size_t begin = 0;
        size_t end = s.size();
        while (begin < end && IsSpace(s[begin])) { ++begin; }
        while (end > begin && IsSpace(s[end - 1])) { --end; }
        return s.substr(begin, end - begin);
    }

    inline bool IsComment(const std::string& trimmed)
    {
        return trimmed.size() >= 2 && trimmed[0] == '/' && trimmed[1] == '/';
    }

    inline ParseResult Parse(const std::string& text)
    {
        ParseResult result;
        std::string section;

        size_t pos = 0;
        int lineNumber = 0;

        while (pos <= text.size())
        {
            const size_t newline = text.find('\n', pos);
            const std::string raw = (newline == std::string::npos)
                ? text.substr(pos)
                : text.substr(pos, newline - pos);

            ++lineNumber;

            // Two views of the line. The trimmed one decides what kind of line it is;
            // the content one is what fields are cut from, so that whitespace a file
            // author put inside a message survives into the message. Only a trailing
            // carriage return is removed, because it belongs to the line ending rather
            // than to the text.
            const std::string trimmed = Trim(raw);
            const std::string content = (!raw.empty() && raw.back() == '\r')
                ? raw.substr(0, raw.size() - 1)
                : raw;

            if (!trimmed.empty() && !IsComment(trimmed))
            {
                if (trimmed.front() == '[' && trimmed.back() == ']' && trimmed.size() >= 2)
                {
                    section = trimmed.substr(1, trimmed.size() - 2);
                }
                else
                {
                    // An '=' before the first ':' makes it an assignment. Order matters:
                    // a colon record's text may well contain '=', and an assignment's
                    // value may contain ':'. Whichever separator comes first decides.
                    const size_t equals = content.find('=');
                    const size_t colon = content.find(':');

                    Record record;
                    record.line = lineNumber;
                    record.section = section;

                    if (equals != std::string::npos && (colon == std::string::npos || equals < colon))
                    {
                        record.isAssignment = true;
                        record.key = Trim(content.substr(0, equals));
                        record.value = content.substr(equals + 1);
                        result.records.push_back(record);
                    }
                    else if (colon != std::string::npos)
                    {
                        record.type = Trim(content.substr(0, colon));

                        size_t fieldStart = colon + 1;
                        for (;;)
                        {
                            const size_t nextColon = content.find(':', fieldStart);
                            if (nextColon == std::string::npos)
                            {
                                record.fields.push_back(content.substr(fieldStart));
                                break;
                            }
                            record.fields.push_back(content.substr(fieldStart, nextColon - fieldStart));
                            fieldStart = nextColon + 1;
                        }
                        result.records.push_back(record);
                    }
                    else
                    {
                        // Neither form. Not silently dropped.
                        result.skippedLines.push_back(lineNumber);
                    }
                }
            }

            if (newline == std::string::npos) { break; }
            pos = newline + 1;
        }

        return result;
    }
}
