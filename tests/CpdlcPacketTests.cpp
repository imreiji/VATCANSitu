// Tests for CpdlcPacket.h.
//
// The old splitting is reproduced verbatim below and run against the same packets, so
// the defect this replaces is demonstrated rather than described.
//
//   cl /EHsc /W4 /std:c++17 tests\CpdlcPacketTests.cpp /Fe:CpdlcPacketTests.exe
//   CpdlcPacketTests.exe

#include "../CpdlcPacket.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << "\n";
        }
    }

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << " - expected \"" << expected
                      << "\", got \"" << actual << "\"\n";
        }
    }

    // The message field as the old code derived it: split on '/', take component five,
    // drop its last character on the assumption that it is the closing brace.
    std::string OldMessageField(const std::string& raw)
    {
        std::stringstream ss(raw);
        std::string token;
        std::vector<std::string> components;
        while (std::getline(ss, token, '/')) { components.push_back(token); }

        if (components.size() < 6) { return "INVALID DOWNLINK MESSAGE"; }
        if (components.at(5).length() <= 1) { return std::string(); }

        std::string message = components.at(5);
        message.pop_back();
        return message;
    }
}

int main()
{
    using namespace SituCpdlcPacket;

    // --- The captured example. Both implementations agree here, which is why the defect
    //     went unnoticed: every message anyone had looked at was a single word.
    {
        const std::string raw = "{/data2/2/19/N/WILCO}";
        const Packet packet = Parse(raw);

        Check(packet.valid, "the captured packet parses");
        CheckEqual(packet.minField, "2", "MIN");
        CheckEqual(packet.mrnField, "19", "MRN");
        CheckEqual(packet.responseAttribute, "N", "response attribute");
        CheckEqual(packet.message, "WILCO", "message");
        CheckEqual(OldMessageField(raw), "WILCO", "and the old code agreed on this one");
    }

    // --- The defect. A slash in the message truncated it there, and took a real
    //     character with it.
    {
        const std::string raw = "{/data2/2/19/N/CTC ON 123.45/126.7}";
        const Packet packet = Parse(raw);

        CheckEqual(packet.message, "CTC ON 123.45/126.7", "a message containing a slash survives whole");
        CheckEqual(OldMessageField(raw), "CTC ON 123.4",
                   "the old code truncated at the slash and ate the 5");
        Check(packet.message != OldMessageField(raw), "the two disagree, which is the point");
    }

    // --- More of the same shape, since this is where a human types a slash.
    CheckEqual(Parse("{/data2/4//WU/DESCEND TO AND MAINTAIN FL240/FL220}").message,
               "DESCEND TO AND MAINTAIN FL240/FL220", "an altitude block");
    CheckEqual(Parse("{/data2/5//R/RWY 06/24 IN USE}").message,
               "RWY 06/24 IN USE", "a runway pair");
    CheckEqual(Parse("{/data2/6//NE/SEE NATTRAK.VATSIM.NET/OCEANIC}").message,
               "SEE NATTRAK.VATSIM.NET/OCEANIC", "a path in a URL");

    // --- An empty MRN is normal: the packet is not a reply.
    {
        const Packet packet = Parse("{/data2/3//NE/ROGER}");
        Check(packet.valid, "an empty MRN is still a valid packet");
        CheckEqual(packet.minField, "3", "MIN with an empty MRN beside it");
        CheckEqual(packet.mrnField, "", "MRN is empty, not absent");
        CheckEqual(packet.responseAttribute, "NE", "response attribute");
        CheckEqual(packet.message, "ROGER", "message");
    }

    // --- Every response attribute in use, uplink and downlink.
    {
        const char* const attributes[] = { "WU", "AN", "R", "NE", "Y", "N" };
        for (const char* attribute : attributes)
        {
            const std::string raw = std::string("{/data2/1/2/") + attribute + "/TEXT}";
            CheckEqual(Parse(raw).responseAttribute, attribute,
                       std::string("response attribute ") + attribute);
        }
    }

    // --- With and without the braces, since the caller may have stripped them.
    {
        CheckEqual(Parse("/data2/2/19/N/WILCO").message, "WILCO", "no braces");
        CheckEqual(Parse("{/data2/2/19/N/WILCO}").message, "WILCO", "braces");
        CheckEqual(Parse("{/data2/2/19/N/}").message, "", "an empty message is empty, not a brace");
    }

    // --- Too few delimiters is not a packet, and must not come back half read.
    {
        const char* const malformed[] = {
            "", "{}", "/data2", "/data2/1", "/data2/1/2", "/data2/1/2/N",
            "{no slashes at all}", "WILCO"
        };
        for (const char* raw : malformed)
        {
            const Packet packet = Parse(raw);
            Check(!packet.valid, std::string("not a packet: \"") + raw + "\"");
            Check(packet.message.empty() && packet.minField.empty(),
                  std::string("and nothing was read out of it: \"") + raw + "\"");
        }
    }

    // --- A message that is only a slash, and one that is only the brace.
    CheckEqual(Parse("{/data2/1/2/N//}").message, "/", "a message of one slash");
    CheckEqual(Parse("{/data2/1/2/N/}").message, "", "no message at all");

    // --- Nothing throws, whatever it is handed.
    {
        const char* const nasty[] = { "", "/", "//", "///", "////", "/////", "//////",
                                      "{", "}", "{}", "{/}", "{//////////}" };
        bool threw = false;
        for (const char* raw : nasty)
        {
            try { Parse(raw); }
            catch (...) { threw = true; std::cout << "   threw on: " << raw << "\n"; }
        }
        Check(!threw, "no input throws");
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
