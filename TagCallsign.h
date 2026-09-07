#pragma once

// Text rules for data tag fields. Pure string work, no SDK types, so the decisions can
// be tested without loading the plugin into EuroScope.
//
// Depends only on the standard library. See tests/TagCallsignTests.cpp.

#include <string>

namespace SituTag
{
    // Whether the transponder code belongs on a correlated aircraft's tag.
    //
    // Normally it does not: a correlated aircraft is squawking the code the flight plan
    // assigned, so printing it again says nothing. An ADS-B correlated target is the
    // exception. Correlation there can come from the aircraft's own broadcast identity
    // rather than from its code, so the code is free to be anything - stale, never
    // assigned, or simply wrong - and nothing on the tag would show it.
    //
    // So it is shown when it disagrees with the assignment, or when there is no
    // assignment to agree with. "0000" counts as no assignment; it is what EuroScope
    // carries for an unassigned code alongside the empty string.
    inline bool ShowsSquawkOnTag(bool isAdsb,
                                 const std::string& transponderCode,
                                 const std::string& assignedCode)
    {
        if (!isAdsb) { return false; }
        if (transponderCode.empty()) { return false; }

        // "0000" means unset on the assignment side only. On the transponder side it is
        // a real observation and one worth showing: an aircraft squawking 0000 against
        // an assignment of 4321 is exactly the disagreement this exists to surface, and
        // one squawking it with nothing assigned is still squawking nothing, which the
        // controller wants to see rather than have hidden as noise.
        const bool assigned = !assignedCode.empty() && assignedCode != "0000";
        if (!assigned) { return true; }

        return transponderCode != assignedCode;
    }

    // The VFR conspicuity code. An aircraft on it is announcing that it is VFR whatever
    // the flight plan says, which is why it counts here alongside the flight plan flag.
    const char* const kVfrSquawk = "1200";

    // What the jurisdiction field shows on the tag - normally a controller's position id.
    //
    // A VFR aircraft nobody is tracking has no position id to show, so the field sits
    // empty and the tag gives no hint why. It shows "VF" instead: the aircraft is VFR and
    // unowned, which is a state rather than an absence.
    //
    // Two guards, and both matter. It only fills a field that is already empty, so a real
    // handoff or tracking id is never overwritten by this - if there is a controller to
    // name, naming them wins. And it requires that nobody is tracking, so an aircraft
    // being worked by someone else is never labelled unowned just because this scope has
    // no handoff in progress with it.
    inline bool ShowsVfrJurisdiction(bool hasVfrFlightPlan,
                                     const std::string& squawk,
                                     const std::string& trackingControllerId,
                                     const std::string& jurisdictionField)
    {
        if (!jurisdictionField.empty()) { return false; }
        if (!trackingControllerId.empty()) { return false; }

        return hasVfrFlightPlan || squawk == kVfrSquawk;
    }
}
