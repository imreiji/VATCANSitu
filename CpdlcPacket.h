#pragma once

// Splitting a Hoppie CPDLC packet into its fields.
//
// The wire form is:
//
//     {/data2/<MIN>/<MRN>/<response attribute>/<message>}
//
// MIN is the sender's message identifier, MRN is the identifier of the message being
// answered and is empty when the packet is not a reply, and the response attribute is
// one of WU, AN, R, NE on an uplink or Y, N on a downlink.
//
// The message is everything after the fifth delimiter, not the sixth of six fields.
// That distinction is the whole reason this exists: the previous code split the packet
// on '/' and took component five, so a message containing a slash - a frequency pair, an
// altitude block, anything a human types - was silently truncated at it. The pop_back()
// that followed assumed a trailing '}', which by then was on a later component, and so
// removed a real character from the message as well. A packet reading
// {/data2/2/19/N/CTC ON 123.45/126.7} arrived as "CTC ON 123.4".
//
// Hoppie's own encoding rules say CPDLC is "only uppercase letters and numbers", so a
// conforming message has no slash in it. But the field is free text on a network its
// operator explicitly does not parse, and pilot clients are heterogeneous. Silent
// truncation of a controller instruction is not a failure mode to leave to convention.
//
// Depends only on <string>. See tests/CpdlcPacketTests.cpp.

#include <string>

namespace SituCpdlcPacket
{
    struct Packet
    {
        // False when the text is not a /data2/ packet with all five delimiters. The
        // caller should treat the message as unparseable rather than partly read.
        bool valid = false;

        std::string minField;      // as written, so the caller decides how to convert
        std::string mrnField;      // empty when this is not a reply
        std::string responseAttribute;
        std::string message;
    };

    // Accepts the packet with or without its surrounding braces.
    inline Packet Parse(const std::string& raw)
    {
        Packet packet;

        // Positions of the first five '/' delimiters.
        size_t delimiters[5] = { 0, 0, 0, 0, 0 };
        int found = 0;

        for (size_t i = 0; i < raw.size() && found < 5; ++i)
        {
            if (raw[i] == '/') { delimiters[found++] = i; }
        }

        if (found < 5) { return packet; }

        packet.minField = raw.substr(delimiters[1] + 1, delimiters[2] - delimiters[1] - 1);
        packet.mrnField = raw.substr(delimiters[2] + 1, delimiters[3] - delimiters[2] - 1);
        packet.responseAttribute = raw.substr(delimiters[3] + 1, delimiters[4] - delimiters[3] - 1);

        // Everything after the last delimiter, less a closing brace if one is there.
        // Checked rather than assumed: the brace is only present when the caller passed
        // the packet with its wrapper still on.
        std::string message = raw.substr(delimiters[4] + 1);
        if (!message.empty() && message.back() == '}') { message.pop_back(); }

        packet.message = message;
        packet.valid = true;
        return packet;
    }
}
