#pragma once

// Deciding what a failed CPDLC fetch means.
//
// Every reply that is not "ok" used to be treated identically: display it, and switch
// CPDLC off until the controller notices and clicks the button again. That is the wrong
// response to most of them.
//
// The HTTP timeout is 2500 ms. Measured from a CZQM controller's machine, a poll of
// hoppie.nl completes in about 0.29 s, so the budget is roughly 8x the normal round trip
// - but a single response over the limit, from any cause, permanently ended the
// controller's datalink for the session. A transient network event should not do that,
// however rare it is.
//
// Hoppie is explicit about one of these. Of "callsign already in use" its documentation
// says: "This is not a technical failure but an operational one, so the client software
// should handle it appropriately." That case arises when another controller holds the
// lock on the station callsign - the lock has a 100 second TTL, so it clears on its own
// when they disconnect, and the right behaviour is to keep polling and say what is
// happening rather than to give up.
//
// What genuinely cannot be retried is a configuration fault: no logon code, a code the
// far end rejects, no station set. Those stay fatal, because polling once a minute
// forever against a credential that will never work is worse than stopping.
//
// Depends only on the standard library. See tests/CpdlcErrorTests.cpp.

#include <string>

namespace SituCpdlcErrors
{
    enum Severity
    {
        // Not an error. Hoppie answers "ok" for an empty poll and "ok " for an empty
        // peek - the trailing space is the only difference, and it is load bearing.
        Ok,

        // The request did not complete, or completed with something we cannot read. Keep
        // polling: the next one will probably work.
        Transient,

        // The far end understood and refused, but the refusal can pass on its own. Keep
        // polling and keep the controller informed.
        Operational,

        // Configuration. Retrying cannot help, so stop and say what to fix.
        Fatal,
    };

    inline std::string Lower(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s) { out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c); }
        return out;
    }

    inline bool Contains(const std::string& haystack, const char* needle)
    {
        return haystack.find(needle) != std::string::npos;
    }

    // Matched on substrings rather than on whole strings, because Hoppie wraps these in
    // both round and curly brackets depending on the path - "error (no logon code)" and
    // "error {invalid logon code}" are both real - and a relay in front of it adds its
    // own in the same format.
    inline Severity Classify(const std::string& reply)
    {
        if (reply.compare(0, 2, "ok") == 0) { return Ok; }

        const std::string text = Lower(reply);

        // Our own wrapper around a failed HTTP request, from PollCPDLCMessages. Nothing
        // reached Hoppie at all, so nothing about the configuration is implicated.
        if (Contains(text, "poll failed")) { return Transient; }

        if (Contains(text, "callsign already in use")) { return Operational; }

        if (Contains(text, "no logon code")
            || Contains(text, "invalid logon code")
            || Contains(text, "logon code not registered")
            || Contains(text, "no from address"))
        {
            return Fatal;
        }

        // Anything unrecognised is treated as transient rather than fatal, and the
        // consecutive-failure count is what stops it running forever. Guessing "fatal"
        // for an unknown string would reintroduce exactly the defect this file exists to
        // remove, for every error message nobody has seen yet.
        return Transient;
    }

    // How many consecutive transient failures before giving up and disabling. At one
    // poll a minute this is roughly ten minutes of trying, which outlasts any network
    // event worth waiting through and still escapes a permanent fault nobody classified.
    const int kTransientFailureLimit = 10;

    // A line for the controller. The raw reply is included because it is the only thing
    // that distinguishes one Hoppie refusal from another, and a controller reporting a
    // problem needs to be able to quote it.
    inline std::string Describe(const std::string& reply, Severity severity)
    {
        switch (severity)
        {
        case Operational:
            return "Another controller currently holds this station callsign on Hoppie. "
                   "Still trying - it clears when they disconnect. (" + reply + ")";
        case Fatal:
            return "CPDLC stopped: " + reply;
        case Transient:
            return "CPDLC fetch failed, still trying: " + reply;
        default:
            return reply;
        }
    }
}
