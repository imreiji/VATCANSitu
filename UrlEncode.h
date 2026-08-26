#pragma once

// Percent-encoding for query string and form body values, per RFC 3986.
//
// This replaces curl_easy_escape, which is the only thing the plugin used libcurl's
// string handling for. It encodes exactly what curl_easy_escape encodes: every octet
// except the unreserved set A-Z a-z 0-9 - . _ ~ is written as %XX with uppercase hex.
//
// It matters that this is exact. The Hoppie logon code and every message body go
// through it, and a character left unencoded in a value - an & or an = - is read by
// the receiving parser as a field separator, in a request whose other fields include
// the logon credential.
//
// Depends on nothing but <string> so it can be tested without MFC or the EuroScope
// SDK. See tests/UrlEncodeTests.cpp.

#include <string>

namespace SituUrl
{
    inline bool IsUnreserved(unsigned char c)
    {
        return (c >= 'A' && c <= 'Z')
            || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9')
            || c == '-' || c == '.' || c == '_' || c == '~';
    }

    inline std::string Encode(const std::string& value)
    {
        static const char* const kHex = "0123456789ABCDEF";

        std::string encoded;
        encoded.reserve(value.size());

        // Indexed over unsigned char: a signed char would make bytes above 0x7F
        // negative, and they must encode as %80..%FF rather than sign extend.
        for (const char raw : value)
        {
            const unsigned char c = static_cast<unsigned char>(raw);
            if (IsUnreserved(c))
            {
                encoded.push_back(static_cast<char>(c));
            }
            else
            {
                encoded.push_back('%');
                encoded.push_back(kHex[c >> 4]);
                encoded.push_back(kHex[c & 0x0F]);
            }
        }
        return encoded;
    }
}
