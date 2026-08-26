// Tests for UrlEncode.h, which replaces curl_easy_escape.
//
// curl_easy_escape is documented as: "All input characters that are not a-z, A-Z, 0-9,
// '-', '.', '_' or '~' are converted to their URL escaped version". These tests assert
// exactly that set, octet by octet across the whole 0..255 range, so the replacement is
// checked against the specification rather than against a handful of examples.
//
// What is actually at stake: the Hoppie logon code and every CPDLC message body are
// encoded with this before going into a request whose other fields include the
// credential. A separator left unencoded inside a value is read by the receiving parser
// as the start of the next field.
//
// Standalone: UrlEncode.h depends only on <string>.
//
//   cl /EHsc /W4 /std:c++17 tests\UrlEncodeTests.cpp /Fe:UrlEncodeTests.exe
//   UrlEncodeTests.exe

#include "../UrlEncode.h"

#include <iostream>
#include <string>

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
}

int main()
{
    using namespace SituUrl;

    // --- The unreserved set passes through untouched, and nothing else does.
    {
        std::string unreservedSeen;
        int encodedCount = 0;

        for (int i = 0; i <= 255; ++i)
        {
            const std::string one(1, static_cast<char>(i));
            const std::string out = Encode(one);

            const bool isUnreserved = (i >= 'A' && i <= 'Z')
                                   || (i >= 'a' && i <= 'z')
                                   || (i >= '0' && i <= '9')
                                   || i == '-' || i == '.' || i == '_' || i == '~';

            if (isUnreserved)
            {
                if (out != one)
                {
                    Check(false, "byte " + std::to_string(i) + " is unreserved and must pass through");
                }
                unreservedSeen.push_back(static_cast<char>(i));
            }
            else
            {
                ++encodedCount;
                if (out.size() != 3 || out[0] != '%')
                {
                    Check(false, "byte " + std::to_string(i) + " must encode as %XX, got \"" + out + "\"");
                }
            }
        }

        Check(unreservedSeen.size() == 66,
              "exactly 66 unreserved octets: 26 + 26 + 10 + 4 (found "
              + std::to_string(unreservedSeen.size()) + ")");
        Check(encodedCount == 190,
              "the other 190 octets are all encoded (found " + std::to_string(encodedCount) + ")");
    }

    // --- Round trip: decoding what we produced returns the original byte, for all 256.
    {
        auto HexValue = [](char c) -> int {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            return -1;
        };

        int roundTripFailures = 0;
        for (int i = 0; i <= 255; ++i)
        {
            const std::string out = Encode(std::string(1, static_cast<char>(i)));
            int decoded = -1;
            if (out.size() == 1)
            {
                decoded = static_cast<unsigned char>(out[0]);
            }
            else if (out.size() == 3 && out[0] == '%')
            {
                const int hi = HexValue(out[1]);
                const int lo = HexValue(out[2]);
                if (hi >= 0 && lo >= 0) { decoded = hi * 16 + lo; }
            }
            if (decoded != i) { ++roundTripFailures; }
        }
        Check(roundTripFailures == 0,
              "every octet round trips (failures: " + std::to_string(roundTripFailures) + ")");
    }

    // --- The separators that made this necessary in the first place.
    CheckEqual(Encode("&"), "%26", "ampersand, which would start a new field");
    CheckEqual(Encode("="), "%3D", "equals, which would start a new value");
    CheckEqual(Encode("?"), "%3F", "question mark");
    CheckEqual(Encode("#"), "%23", "hash");
    CheckEqual(Encode("+"), "%2B", "plus, which a form decoder would read as a space");
    CheckEqual(Encode(" "), "%20", "space encodes as %20, not as +");
    CheckEqual(Encode("/"), "%2F", "slash");

    // --- A CPDLC packet encoded whole, the way SendCPDLCMessage sends it. The slashes
    //     that give it its structure are inside one field, so they must be encoded.
    CheckEqual(Encode("/data2/3//NE/CLIMB TO FL350"),
               "%2Fdata2%2F3%2F%2FNE%2FCLIMB%20TO%20FL350",
               "a CPDLC packet");

    // --- An injection attempt in a message body cannot reach the field structure.
    {
        const std::string hostile = "HELLO&logon=stolen&to=SOMEONE";
        const std::string encoded = Encode(hostile);
        Check(encoded.find('&') == std::string::npos, "no bare ampersand survives encoding");
        Check(encoded.find('=') == std::string::npos, "no bare equals survives encoding");
        CheckEqual(encoded, "HELLO%26logon%3Dstolen%26to%3DSOMEONE", "the whole hostile value");
    }

    // --- Bytes above 0x7F encode as %80..%FF rather than sign extending, which is what
    //     iterating over a signed char would have produced.
    CheckEqual(Encode(std::string(1, static_cast<char>(0x80))), "%80", "0x80");
    CheckEqual(Encode(std::string(1, static_cast<char>(0xFF))), "%FF", "0xFF");
    CheckEqual(Encode("caf\xC3\xA9"), "caf%C3%A9", "UTF-8 e-acute encodes as two octets");

    // --- Embedded NUL is a byte like any other; std::string carries it and it must not
    //     truncate the result the way a strlen based encoder would.
    {
        std::string withNul("AB", 2);
        withNul.push_back('\0');
        withNul += "CD";
        CheckEqual(Encode(withNul), "AB%00CD", "an embedded NUL encodes rather than truncates");
    }

    // --- Nothing to do.
    CheckEqual(Encode(""), "", "empty input");
    CheckEqual(Encode("AZaz09-._~"), "AZaz09-._~", "an all-unreserved value is unchanged");

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";

    if (g_failures != 0)
    {
        std::cout << g_failures << " FAILURES\n";
        return 1;
    }

    std::cout << "OK\n";
    return 0;
}
