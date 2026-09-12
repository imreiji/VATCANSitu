#pragma once

// The rules behind the Alt window: what a typed or picked entry means as a EuroScope
// cleared altitude, what the field shows for a stored one, the rows the list offers and
// which of them the window opens on.
//
// EuroScope's cleared altitude is an int in feet with three reserved values that are
// not heights: 0 none, 1 cleared for an ILS approach, 2 cleared for a visual approach.
// The window shows those as CLR, CA and VA, which is what a CanScope Alt window shows.
//
// Depends only on the standard library. See tests/AltitudeEntryTests.cpp.

#include <string>
#include <vector>

namespace SituAltitude
{
    const int kApproachIls = 1;
    const int kApproachVisual = 2;
    const int kTopLevel = 600;      // hundreds of feet
    const int kBottomLevel = 10;
    const int kLevelStep = 10;
    const int kVisibleRows = 7;

    struct Entry
    {
        bool ok = false;
        int clearedAltitude = 0;
    };

    inline std::string TrimUpper(const std::string& text)
    {
        size_t a = 0;
        size_t b = text.size();
        while (a < b && (text[a] == ' ' || text[a] == '\t')) { ++a; }
        while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) { --b; }
        std::string out = text.substr(a, b - a);
        for (char& c : out) { if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); } }
        return out;
    }

    // Exactly three digits are hundreds of feet - the unit the tag shows. Anything
    // shorter or longer is refused rather than guessed at: "36" could be 3600 or 36000,
    // and a wrong clearance is worse than a rejected one.
    inline Entry Parse(const std::string& text)
    {
        Entry entry;
        const std::string t = TrimUpper(text);

        if (t.empty() || t == "CLR") { entry.ok = true; entry.clearedAltitude = 0; return entry; }
        if (t == "CA") { entry.ok = true; entry.clearedAltitude = kApproachIls; return entry; }
        if (t == "VA") { entry.ok = true; entry.clearedAltitude = kApproachVisual; return entry; }

        if (t.size() != 3) { return entry; }
        int hundreds = 0;
        for (char c : t)
        {
            if (c < '0' || c > '9') { return entry; }
            hundreds = hundreds * 10 + (c - '0');
        }
        entry.ok = true;
        entry.clearedAltitude = hundreds * 100;
        return entry;
    }

    inline std::string ThreeDigits(int hundreds)
    {
        std::string s = std::to_string(hundreds);
        while (s.size() < 3) { s.insert(s.begin(), '0'); }
        return s;
    }

    // What the field shows for a stored cleared altitude. None shows empty rather than
    // CLR: an empty field reads as "nothing set", which is what it is.
    inline std::string EntryFor(int clearedAltitude)
    {
        if (clearedAltitude == kApproachIls) { return "CA"; }
        if (clearedAltitude == kApproachVisual) { return "VA"; }
        if (clearedAltitude <= 0) { return ""; }
        return ThreeDigits(clearedAltitude / 100);
    }

    // 600 down to 010 in tens, then the three words.
    inline std::vector<std::string> ListRows()
    {
        std::vector<std::string> rows;
        for (int level = kTopLevel; level >= kBottomLevel; level -= kLevelStep)
        {
            rows.push_back(ThreeDigits(level));
        }
        rows.push_back("CA");
        rows.push_back("VA");
        rows.push_back("CLR");
        return rows;
    }

    inline int LevelCount() { return (kTopLevel - kBottomLevel) / kLevelStep + 1; }

    inline int RowForLevel(int hundreds)
    {
        // Nearest ten, clamped to the list.
        int rounded = ((hundreds + kLevelStep / 2) / kLevelStep) * kLevelStep;
        if (rounded < kBottomLevel) { rounded = kBottomLevel; }
        if (rounded > kTopLevel) { rounded = kTopLevel; }
        return (kTopLevel - rounded) / kLevelStep;
    }

    // The row to highlight when the window opens. A real cleared altitude selects its
    // own row. None, or an approach clearance, selects the aircraft's current level so
    // the list opens where the aircraft is rather than at the top.
    inline int RowFor(int clearedAltitude, int currentAltitudeFt)
    {
        const bool isLevel = clearedAltitude > kApproachVisual;
        const int feet = isLevel ? clearedAltitude : currentAltitudeFt;
        return RowForLevel(feet / 100);
    }

    // The first row of a view of `visible` rows so that `selected` sits in the middle,
    // clamped so the view never runs off either end of the list.
    inline int FirstVisibleRow(int selected, int visible, int total)
    {
        if (total <= visible) { return 0; }
        int first = selected - visible / 2;
        if (first < 0) { first = 0; }
        if (first > total - visible) { first = total - visible; }
        return first;
    }

    enum class Uplink { None, Climb, Descend };

    // Which CPDLC instruction a new clearance is, judged against where the aircraft is.
    // The reserved values are not levels and get no uplink.
    inline Uplink UplinkFor(int clearedAltitude, int currentAltitudeFt)
    {
        if (clearedAltitude <= kApproachVisual) { return Uplink::None; }
        if (clearedAltitude > currentAltitudeFt) { return Uplink::Climb; }
        if (clearedAltitude < currentAltitudeFt) { return Uplink::Descend; }
        return Uplink::None;
    }
}
