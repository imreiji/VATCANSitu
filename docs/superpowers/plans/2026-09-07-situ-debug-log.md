# SituDebug Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A file-backed, off-by-default diagnostic log of every callback the plugin receives from EuroScope, every state change it makes, every load and fetch, and on-change draw-decision snapshots for followed aircraft, controlled by `.situ log` chat commands.

**Architecture:** Pure logic (line formatter, command parser, rotation rule, draw snapshot) lives in a header-only `SituLogCore.h` that depends only on the standard library, so it compiles in the standalone test harness. The file sink, mutex and process-wide state live in `SituLog.h`/`SituLog.cpp` inside the DLL. Call sites in `SituPlugin.cpp`, `CSiTRadar.cpp`, `CSiTRadar.h`, `ACTag.cpp`, `PPS.h` and `wxRadar.cpp` each add one line.

**Tech Stack:** C++17, MSVC v142, Win32 (`GetSystemTime`), `std::ofstream`, `std::mutex`. Tests are single-file executables compiled with `cl /EHsc /W4 /std:c++17`.

**Spec:** `docs/superpowers/specs/2026-09-07-situ-debug-log-design.md`

## Global Constraints

- Every `.cpp` includes `"pch.h"` first. New `.cpp` files are added to the `<ClCompile>` ItemGroup in `VATCANSitu.vcxproj` (around line 110).
- Test files include only headers that depend on the standard library. They must compile with exactly: `cl /nologo /EHsc /W4 /std:c++17 tests\<Name>.cpp /Fe:testbin\<Name>.exe /Fo:testbin\` from the repo root in a VS x86 dev shell. `/W4` warnings are acceptable; errors are not.
- Nothing in `SituLog` calls the EuroScope SDK, creates GDI objects, throws out of its entry points, or blocks on anything but its own mutex.
- Log lines are written only when logging is enabled. Startup state is off, following nothing.
- The file lives at `wxRadar::getSituWxDir() + "SituDebug-YYYYMMDD-HHMMSS.log"`. At most five such files are kept.
- Line format: `HH:MM:SS.mmm CAT  SUBJECT     key=value ...` with CAT padded to 4 and SUBJECT padded to 11, values quoted when they contain space, `"` or `=`, internal quotes doubled, empty values written as `key=`, booleans as `0`/`1`.
- Build check for DLL tasks: `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" VATCANSitu.vcxproj /p:Configuration=Debug /p:Platform=Win32 /v:m /nologo` must end with no `error` lines. Run it from PowerShell in the repo root.
- Dev shell for tests, from PowerShell: `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch x86 -SkipAutomaticLocation` then `Set-Location` back to the repo root.
- Commit messages end with the two attribution lines: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01Qth1BrA73xsnTBoR6iQKRY`.
- Do not commit `PPS.h`'s pre-existing uncommitted change (a 1200 test in the Mode S fallback) as part of any task except Task 10, which owns `PPS.h`. Do not commit `CLAUDE.md`, `CODE_REVIEW.md` or `CPDLC_REVIEW.md`.

---

## File map

| File | Responsibility | Tasks |
|---|---|---|
| `SituLogCore.h` (new) | Pure: `Fields`, `FormatLine`, `ParseLogCommand`, `RotationVictims`, `DrawSnapshot`, `FormatSnapshot`. Standard library only. | 1–4 |
| `tests/SituLogTests.cpp` (new) | Tests for everything in `SituLogCore.h`. | 1–4 |
| `SituLog.h` / `SituLog.cpp` (new) | File sink, mutex, enabled flag, followed set, snapshot map, timestamp. Public API used by call sites. | 5 |
| `VATCANSitu.vcxproj` | Add `SituLog.cpp`. | 5 |
| `SituPlugin.h` / `SituPlugin.cpp` | `OnCompileCommand`; EVT lines in plugin-level callbacks; ES> in scratchpad writes. | 6, 7, 8 |
| `CSiTRadar.cpp` / `CSiTRadar.h` | EVT lines in screen callbacks; ES> at outbound calls; NET at file loads; WARN twins; DRAW snapshot in both draw loops. | 7, 8, 9, 10 |
| `wxRadar.cpp` | NET lines from worker threads. | 9 |
| `PPS.h` | `DrawPPS` returns the shape name. | 10 |
| `ACTag.cpp` / `ACTag.h` | Tag routines report which tag form they drew. | 10 |
| `.github/workflows/build-prerelease.yml` | Add the test to CI. | 11 |
| `README.md` | Document the commands. | 11 |

---

### Task 1: Line formatter

**Files:**
- Create: `SituLogCore.h`
- Create: `tests/SituLogTests.cpp`

**Interfaces:**
- Produces: `namespace SituLog { struct Fields; std::string FormatLine(const std::string& time, const char* cat, const std::string& subject, const Fields& fields); std::string QuoteValue(const std::string&); }`
- `Fields` has `Fields& Add(const std::string& key, const std::string& value)`, `Fields& Add(const std::string& key, int value)`, `Fields& Add(const std::string& key, bool value)`, `Fields& Add(const std::string& key, double value)` and `const std::vector<std::pair<std::string,std::string>>& Items() const`.

- [ ] **Step 1: Write the failing test**

Create `tests/SituLogTests.cpp`:

```cpp
// Tests for SituLogCore.h.
//
//   cl /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:SituLogTests.exe
//   SituLogTests.exe

#include "../SituLogCore.h"

#include <iostream>
#include <string>

namespace
{
    int g_checks = 0;
    int g_failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++g_checks;
        if (!condition) { ++g_failures; std::cout << "FAIL: " << what << "\n"; }
    }

    void CheckEqual(const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++g_checks;
        if (actual != expected)
        {
            ++g_failures;
            std::cout << "FAIL: " << what << "\n   expected: \"" << expected << "\"\n   got:      \"" << actual << "\"\n";
        }
    }
}

int main()
{
    using namespace SituLog;

    // --- Formatter. Column widths are fixed so grep and eye both find the field.
    {
        CheckEqual(FormatLine("02:15:58.412", "ES>", "CORRELATE", Fields().Add("callsign", "ACA123").Add("squawk", "4521")),
                   "02:15:58.412 ES>  CORRELATE   callsign=ACA123 squawk=4521",
                   "two plain fields");

        CheckEqual(FormatLine("02:15:58.412", "DRAW", "ACA123", Fields()),
                   "02:15:58.412 DRAW ACA123      ",
                   "no fields still pads the subject");

        // Category is padded to 4, subject to 11, each followed by one space.
        CheckEqual(FormatLine("00:00:00.000", "CMD", "log", Fields().Add("args", "on")),
                   "00:00:00.000 CMD  log         args=on",
                   "short category and subject padded");

        // A subject longer than 11 is not truncated.
        CheckEqual(FormatLine("00:00:00.000", "EVT", "OnGetTagItemXYZ", Fields()),
                   "00:00:00.000 EVT  OnGetTagItemXYZ ",
                   "long subject kept whole, one trailing space");

        // Quoting: space, quote and equals each force quotes; quotes inside are doubled.
        CheckEqual(QuoteValue("YHZ MIILS"), "\"YHZ MIILS\"", "space quoted");
        CheckEqual(QuoteValue("a=b"), "\"a=b\"", "equals quoted");
        CheckEqual(QuoteValue("say \"hi\""), "\"say \"\"hi\"\"\"", "quote doubled and quoted");
        CheckEqual(QuoteValue("plain"), "plain", "plain not quoted");
        CheckEqual(QuoteValue(""), "", "empty stays empty");

        CheckEqual(FormatLine("00:00:00.000", "EVT", "FP-DATA", Fields().Add("rmk", "").Add("route", "A B")),
                   "00:00:00.000 EVT  FP-DATA     rmk= route=\"A B\"",
                   "empty value visible as key=, spaced value quoted");

        // Typed adds.
        CheckEqual(FormatLine("00:00:00.000", "DRAW", "X", Fields().Add("corr", true).Add("adsb", false).Add("flags", 6).Add("ms", 12.5)),
                   "00:00:00.000 DRAW X           corr=1 adsb=0 flags=6 ms=12.5",
                   "bool as 0/1, int, double");

        // Route truncation helper: 60 characters then "...".
        const std::string longRoute(70, 'R');
        CheckEqual(Truncate(longRoute, 60), std::string(60, 'R') + "...", "truncated at 60 with ellipsis");
        CheckEqual(Truncate("short", 60), "short", "short untouched");
    }

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    if (g_failures != 0) { std::cout << g_failures << " FAILURES\n"; return 1; }
    std::cout << "OK\n";
    return 0;
}
```

- [ ] **Step 2: Run it to verify it fails**

From the dev shell in the repo root:
```
cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\
```
Expected: compile error, `Cannot open include file: '../SituLogCore.h'`.

- [ ] **Step 3: Write the formatter**

Create `SituLogCore.h`:

```cpp
#pragma once

// The pure half of the SituDebug log: line formatting, command parsing, rotation and the
// draw-decision snapshot. No I/O, no SDK, no Windows headers, so every rule here is
// testable without loading the plugin. The file sink and process state are in SituLog.h.
//
// See docs/superpowers/specs/2026-09-07-situ-debug-log-design.md and tests/SituLogTests.cpp.

#include <string>
#include <utility>
#include <vector>
#include <cstdio>

namespace SituLog
{
    // key=value pairs for one line, in insertion order. Built inline at call sites:
    //   Fields().Add("callsign", cs).Add("squawk", sq)
    class Fields
    {
    public:
        Fields& Add(const std::string& key, const std::string& value)
        {
            items_.emplace_back(key, value);
            return *this;
        }
        Fields& Add(const std::string& key, const char* value)
        {
            return Add(key, std::string(value != nullptr ? value : ""));
        }
        Fields& Add(const std::string& key, bool value)
        {
            return Add(key, std::string(value ? "1" : "0"));
        }
        Fields& Add(const std::string& key, int value)
        {
            return Add(key, std::to_string(value));
        }
        Fields& Add(const std::string& key, double value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%g", value);
            return Add(key, std::string(buffer));
        }

        const std::vector<std::pair<std::string, std::string>>& Items() const { return items_; }

    private:
        std::vector<std::pair<std::string, std::string>> items_;
    };

    // A value is quoted when a reader could otherwise mis-split it: a space ends a pair, an
    // equals sign starts a value, a quote opens one. Internal quotes are doubled, CSV style.
    inline std::string QuoteValue(const std::string& value)
    {
        bool needsQuotes = false;
        for (char c : value)
        {
            if (c == ' ' || c == '"' || c == '=') { needsQuotes = true; break; }
        }
        if (!needsQuotes) { return value; }

        std::string out;
        out.reserve(value.size() + 2);
        out += '"';
        for (char c : value)
        {
            if (c == '"') { out += '"'; }
            out += c;
        }
        out += '"';
        return out;
    }

    inline std::string Truncate(const std::string& text, size_t max)
    {
        if (text.size() <= max) { return text; }
        return text.substr(0, max) + "...";
    }

    inline std::string PadRight(const std::string& text, size_t width)
    {
        if (text.size() >= width) { return text; }
        return text + std::string(width - text.size(), ' ');
    }

    // HH:MM:SS.mmm CAT  SUBJECT     key=value ...
    // The caller supplies the time so this stays clock-free.
    inline std::string FormatLine(const std::string& time,
                                  const char* cat,
                                  const std::string& subject,
                                  const Fields& fields)
    {
        std::string line = time;
        line += ' ';
        line += PadRight(cat != nullptr ? cat : "", 4);
        line += ' ';
        line += PadRight(subject, 11);
        line += ' ';

        bool first = true;
        for (const auto& item : fields.Items())
        {
            if (!first) { line += ' '; }
            first = false;
            line += item.first;
            line += '=';
            line += QuoteValue(item.second);
        }
        return line;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

```
cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\
testbin\SituLogTests.exe
```
Expected: `13/13 checks passed` then `OK`.

- [ ] **Step 5: Commit**

```
git add SituLogCore.h tests/SituLogTests.cpp
git commit -m "SituLog: line formatter with fixed columns and quoted values"
```
(append the two attribution lines)

---

### Task 2: Command parser

**Files:**
- Modify: `SituLogCore.h` (append inside the namespace)
- Modify: `tests/SituLogTests.cpp`

**Interfaces:**
- Produces: `enum class LogAction { NotOurs, Help, On, Off, Follow, FollowAll, Unfollow, Status }; struct LogCommand { LogAction action; std::string arg; }; LogCommand ParseLogCommand(const std::string& commandLine);`

- [ ] **Step 1: Add the failing tests**

Insert before the `std::cout << "\n" << (g_checks - g_failures)` line in `tests/SituLogTests.cpp`:

```cpp
    // --- Command parser. Anything not beginning ".situ log" is not ours and must not be
    //     claimed, or the plugin would swallow other plugins' commands and chat.
    {
        Check(ParseLogCommand(".situ log on").action == LogAction::On, "on");
        Check(ParseLogCommand(".situ log off").action == LogAction::Off, "off");
        Check(ParseLogCommand(".situ log all").action == LogAction::FollowAll, "all");
        Check(ParseLogCommand(".situ log none").action == LogAction::Unfollow, "none");
        Check(ParseLogCommand(".situ log status").action == LogAction::Status, "status");
        Check(ParseLogCommand(".situ log").action == LogAction::Help, "bare gives help");
        Check(ParseLogCommand(".situ log   ").action == LogAction::Help, "bare with spaces gives help");
        Check(ParseLogCommand(".situ log on extra").action == LogAction::Help, "extra words give help");

        // Keywords are case-insensitive; so is the prefix.
        Check(ParseLogCommand(".SITU LOG ON").action == LogAction::On, "upper-case keyword");
        Check(ParseLogCommand("  .situ log off").action == LogAction::Off, "leading spaces");

        // A callsign is anything that is not a keyword, upper-cased.
        {
            const LogCommand c = ParseLogCommand(".situ log aca123");
            Check(c.action == LogAction::Follow, "callsign follows");
            CheckEqual(c.arg, "ACA123", "callsign upper-cased");
        }
        {
            const LogCommand c = ParseLogCommand(".situ log CGNQC");
            Check(c.action == LogAction::Follow && c.arg == "CGNQC", "registration follows");
        }

        // Not ours.
        Check(ParseLogCommand(".situ").action == LogAction::NotOurs, ".situ alone is not ours");
        Check(ParseLogCommand(".situlog on").action == LogAction::NotOurs, "no space is not ours");
        Check(ParseLogCommand(".am ACA123").action == LogAction::NotOurs, "other dot command");
        Check(ParseLogCommand("hello").action == LogAction::NotOurs, "chat");
        Check(ParseLogCommand("").action == LogAction::NotOurs, "empty");
    }
```

- [ ] **Step 2: Run to verify it fails**

```
cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\
```
Expected: errors, `'ParseLogCommand': identifier not found`.

- [ ] **Step 3: Write the parser**

Append inside `namespace SituLog` in `SituLogCore.h`, after `FormatLine`:

```cpp
    enum class LogAction { NotOurs, Help, On, Off, Follow, FollowAll, Unfollow, Status };

    struct LogCommand
    {
        LogAction action = LogAction::NotOurs;
        std::string arg;
    };

    inline std::string ToUpper(std::string s)
    {
        for (char& c : s) { if (c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); } }
        return s;
    }

    inline std::vector<std::string> SplitWords(const std::string& text)
    {
        std::vector<std::string> words;
        std::string current;
        for (char c : text)
        {
            if (c == ' ' || c == '\t')
            {
                if (!current.empty()) { words.push_back(current); current.clear(); }
            }
            else { current += c; }
        }
        if (!current.empty()) { words.push_back(current); }
        return words;
    }

    // ".situ log <word>" -> an action. Exactly one word is accepted after the prefix; more
    // is a typo and gets the help text rather than a guess.
    inline LogCommand ParseLogCommand(const std::string& commandLine)
    {
        LogCommand command;

        const std::vector<std::string> words = SplitWords(commandLine);
        if (words.size() < 2) { return command; }
        if (ToUpper(words[0]) != ".SITU" || ToUpper(words[1]) != "LOG") { return command; }

        if (words.size() == 2) { command.action = LogAction::Help; return command; }
        if (words.size() > 3) { command.action = LogAction::Help; return command; }

        const std::string word = ToUpper(words[2]);
        if (word == "ON")          { command.action = LogAction::On; }
        else if (word == "OFF")    { command.action = LogAction::Off; }
        else if (word == "ALL")    { command.action = LogAction::FollowAll; }
        else if (word == "NONE")   { command.action = LogAction::Unfollow; }
        else if (word == "STATUS") { command.action = LogAction::Status; }
        else
        {
            command.action = LogAction::Follow;
            command.arg = word;
        }
        return command;
    }
```

- [ ] **Step 4: Run the tests**

```
cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\
testbin\SituLogTests.exe
```
Expected: `30/30 checks passed`, `OK`.

- [ ] **Step 5: Commit**

```
git add SituLogCore.h tests/SituLogTests.cpp
git commit -m "SituLog: parse .situ log commands"
```

---

### Task 3: Rotation rule

**Files:**
- Modify: `SituLogCore.h`
- Modify: `tests/SituLogTests.cpp`

**Interfaces:**
- Produces: `std::vector<std::string> RotationVictims(std::vector<std::string> names, size_t keep);` and `bool IsLogFileName(const std::string& name);` and `std::string LogFileName(const std::string& yyyymmdd_hhmmss);`

- [ ] **Step 1: Add the failing tests**

Insert before the summary line in `tests/SituLogTests.cpp`:

```cpp
    // --- Rotation. Given the names in the folder, which to delete so that after the new
    //     file is created at most five exist. keep is the number of OLD files to keep (4).
    {
        Check(IsLogFileName("SituDebug-20260907-021558.log"), "canonical name matches");
        Check(!IsLogFileName("settings.txt"), "settings not a log");
        Check(!IsLogFileName("SituDebug-20260907-021558.log.bak"), "wrong suffix");
        Check(!IsLogFileName("Debug-20260907.log"), "wrong prefix");
        CheckEqual(LogFileName("20260907-021558"), "SituDebug-20260907-021558.log", "name built from stamp");

        std::vector<std::string> none;
        Check(RotationVictims(none, 4).empty(), "empty folder, nothing to delete");

        std::vector<std::string> four = {
            "SituDebug-20260901-000000.log", "SituDebug-20260902-000000.log",
            "SituDebug-20260903-000000.log", "SituDebug-20260904-000000.log" };
        Check(RotationVictims(four, 4).empty(), "four old files, all kept");

        std::vector<std::string> five = four;
        five.push_back("SituDebug-20260905-000000.log");
        {
            const std::vector<std::string> v = RotationVictims(five, 4);
            Check(v.size() == 1 && v[0] == "SituDebug-20260901-000000.log", "five old files, oldest deleted");
        }

        // Order in the input does not matter; names sort by their timestamp.
        std::vector<std::string> shuffled = {
            "SituDebug-20260905-000000.log", "SituDebug-20260901-000000.log",
            "SituDebug-20260903-000000.log", "SituDebug-20260902-000000.log",
            "SituDebug-20260904-000000.log", "SituDebug-20260906-120000.log",
            "SituDebug-20260906-110000.log", "SituDebug-20260907-000000.log",
            "SituDebug-20260908-000000.log" };
        {
            const std::vector<std::string> v = RotationVictims(shuffled, 4);
            Check(v.size() == 5, "nine old files, five deleted");
            Check(v.size() == 5 && v[0] == "SituDebug-20260901-000000.log", "oldest first");
            Check(v.size() == 5 && v[4] == "SituDebug-20260906-110000.log", "same day, earlier time deleted");
        }

        // Other files in the folder are never victims.
        std::vector<std::string> mixed = five;
        mixed.push_back("settings.txt");
        mixed.push_back("SituLocal.txt");
        mixed.push_back("0_0.png");
        {
            const std::vector<std::string> v = RotationVictims(mixed, 4);
            Check(v.size() == 1 && v[0] == "SituDebug-20260901-000000.log", "non-log files untouched");
        }
    }
```

- [ ] **Step 2: Run to verify it fails**

Expected: errors, `'IsLogFileName': identifier not found`.

- [ ] **Step 3: Write the rotation rule**

Append inside the namespace:

```cpp
    inline const char* const kLogPrefix = "SituDebug-";
    inline const char* const kLogSuffix = ".log";

    inline bool IsLogFileName(const std::string& name)
    {
        const std::string prefix = kLogPrefix;
        const std::string suffix = kLogSuffix;
        if (name.size() <= prefix.size() + suffix.size()) { return false; }
        if (name.compare(0, prefix.size(), prefix) != 0) { return false; }
        return name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    inline std::string LogFileName(const std::string& stamp)
    {
        return std::string(kLogPrefix) + stamp + kLogSuffix;
    }

    // Names sort lexically, and the stamp is zero-padded yyyymmdd-hhmmss, so lexical order
    // is time order. Everything but the newest `keep` is a victim, oldest first.
    inline std::vector<std::string> RotationVictims(std::vector<std::string> names, size_t keep)
    {
        std::vector<std::string> logs;
        for (const std::string& name : names)
        {
            if (IsLogFileName(name)) { logs.push_back(name); }
        }
        std::sort(logs.begin(), logs.end());

        if (logs.size() <= keep) { return {}; }
        logs.resize(logs.size() - keep);
        return logs;
    }
```

Add `#include <algorithm>` to the includes at the top of `SituLogCore.h`.

- [ ] **Step 4: Run the tests**

Expected: `43/43 checks passed`, `OK`.

- [ ] **Step 5: Commit**

```
git add SituLogCore.h tests/SituLogTests.cpp
git commit -m "SituLog: rotation keeps the newest five log files"
```

---

### Task 4: Draw snapshot

**Files:**
- Modify: `SituLogCore.h`
- Modify: `tests/SituLogTests.cpp`

**Interfaces:**
- Produces:
```cpp
struct DrawSnapshot {
    int flags = 0; bool corr = false; bool adsb = false; bool rvsm = false; bool vfr = false;
    std::string sqk; std::string trk; int tag = 0;
    std::string pps;     // NONE TRIANGLE TRIANGLE-FILLED Y HEXAGON DIAMOND CIRCLE-CHECK ASTERISK SQUARE SQUARE-BAR SQUARE-RAYS
    std::string colour;  // YELLOW ORANGE MAGENTA RED WHITE
    bool vf = false;
    std::string tagfn;   // ALPHA BRAVO UNCORR UNCORR-ADSB NARDS NONE
};
bool operator==(const DrawSnapshot&, const DrawSnapshot&);
bool operator!=(const DrawSnapshot&, const DrawSnapshot&);
Fields SnapshotFields(const DrawSnapshot&);
```

- [ ] **Step 1: Add the failing tests**

```cpp
    // --- Draw snapshot. Equality is field-by-field; the fields list is the full state.
    {
        DrawSnapshot a;
        a.flags = 6; a.corr = true; a.sqk = "4521"; a.trk = "QM"; a.tag = 1;
        a.pps = "HEXAGON"; a.colour = "YELLOW"; a.tagfn = "ALPHA";

        DrawSnapshot b = a;
        Check(a == b, "copy is equal");
        Check(!(a != b), "copy is not unequal");

        b.sqk = "1200";
        Check(a != b, "squawk change is a change");

        b = a; b.pps = "ASTERISK";
        Check(a != b, "shape change is a change");

        b = a; b.vf = true;
        Check(a != b, "vf change is a change");

        b = a; b.flags = 7;
        Check(a != b, "flags change is a change");

        CheckEqual(FormatLine("00:00:00.000", "DRAW", "ACA123", SnapshotFields(a)),
                   "00:00:00.000 DRAW ACA123      flags=6 corr=1 adsb=0 rvsm=0 vfr=0 sqk=4521 trk=QM tag=1 pps=HEXAGON colour=YELLOW vf=0 tagfn=ALPHA",
                   "snapshot fields in the documented order");
    }
```

Note the six spaces after `ACA123`: the subject is padded to 11 (`ACA123` plus five spaces) and then one separator space.

- [ ] **Step 2: Run to verify it fails**

Expected: errors, `'DrawSnapshot': undeclared identifier`.

- [ ] **Step 3: Write the snapshot**

Append inside the namespace:

```cpp
    // The inputs the symbology depends on and the outputs it produced, for one aircraft on
    // one frame. Logged only when it differs from the last one logged for that aircraft.
    struct DrawSnapshot
    {
        int flags = 0;
        bool corr = false;
        bool adsb = false;
        bool rvsm = false;
        bool vfr = false;
        std::string sqk;
        std::string trk;
        int tag = 0;
        std::string pps;
        std::string colour;
        bool vf = false;
        std::string tagfn;
    };

    inline bool operator==(const DrawSnapshot& a, const DrawSnapshot& b)
    {
        return a.flags == b.flags && a.corr == b.corr && a.adsb == b.adsb && a.rvsm == b.rvsm
            && a.vfr == b.vfr && a.sqk == b.sqk && a.trk == b.trk && a.tag == b.tag
            && a.pps == b.pps && a.colour == b.colour && a.vf == b.vf && a.tagfn == b.tagfn;
    }

    inline bool operator!=(const DrawSnapshot& a, const DrawSnapshot& b) { return !(a == b); }

    inline Fields SnapshotFields(const DrawSnapshot& s)
    {
        return Fields()
            .Add("flags", s.flags).Add("corr", s.corr).Add("adsb", s.adsb).Add("rvsm", s.rvsm)
            .Add("vfr", s.vfr).Add("sqk", s.sqk).Add("trk", s.trk).Add("tag", s.tag)
            .Add("pps", s.pps).Add("colour", s.colour).Add("vf", s.vf).Add("tagfn", s.tagfn);
    }
```

- [ ] **Step 4: Run the tests**

Expected: `50/50 checks passed`, `OK`.

- [ ] **Step 5: Commit**

```
git add SituLogCore.h tests/SituLogTests.cpp
git commit -m "SituLog: draw-decision snapshot with field-by-field equality"
```

---

### Task 5: File sink and process state

**Files:**
- Create: `SituLog.h`, `SituLog.cpp`
- Modify: `VATCANSitu.vcxproj` (ClCompile ItemGroup near line 110)

**Interfaces:**
- Consumes: everything from `SituLogCore.h`.
- Produces (all in `namespace SituLog`, declared in `SituLog.h`):
```cpp
struct EnableResult { bool ok; std::string path; std::string error; };
EnableResult Enable(const std::string& dir);    // dir ends with a backslash
void   Disable();
bool   IsEnabled();
void   Follow(const std::string& callsign);     // "ALL" follows everything; implies nothing about Enable
void   Unfollow();
bool   IsFollowed(const std::string& callsign);
bool   FollowingAll();
void   Line(const char* cat, const std::string& subject, const Fields& fields);
void   Warn(const std::string& subject, const std::string& text);   // Line("WARN", subject, Fields().Add("text", text))
void   Draw(const std::string& callsign, const DrawSnapshot& now); // compare with last, log on change
std::string Status();
size_t LinesWritten();
```

- [ ] **Step 1: Write the header**

Create `SituLog.h`:

```cpp
#pragma once

// SituDebug log: the file sink and process-wide state. Off until Enable() succeeds.
//
// Rules, all of which the call sites depend on:
//   - Nothing here calls the EuroScope SDK or touches GDI. Worker threads may call Line().
//   - No entry point throws. A write failure disables the log; Status() says so.
//   - Line() is a cheap boolean test when the log is off, so call sites need no guard.
//
// The pure parts - formatting, parsing, rotation, the snapshot - are in SituLogCore.h.

#include "SituLogCore.h"

#include <string>

namespace SituLog
{
    struct EnableResult
    {
        bool ok = false;
        std::string path;
        std::string error;
    };

    // Rotates old logs in dir, opens a new timestamped file, enables logging. dir must end
    // with a path separator. On failure the log stays off and error says why.
    EnableResult Enable(const std::string& dir);
    void Disable();
    bool IsEnabled();

    void Follow(const std::string& callsign);   // "ALL" follows every aircraft
    void Unfollow();                            // empties the set and forgets snapshots
    bool IsFollowed(const std::string& callsign);
    bool FollowingAll();

    void Line(const char* cat, const std::string& subject, const Fields& fields);
    void Warn(const std::string& subject, const std::string& text);

    // Logs a DRAW line when the snapshot differs from the last one logged for this
    // callsign, or when none has been. Does nothing unless the callsign is followed.
    void Draw(const std::string& callsign, const DrawSnapshot& now);

    std::string Status();
    size_t LinesWritten();
}
```

- [ ] **Step 2: Write the implementation**

Create `SituLog.cpp`:

```cpp
#include "pch.h"
#include "SituLog.h"

#include <windows.h>

#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace
{
    std::mutex g_mutex;
    std::ofstream g_file;
    bool g_enabled = false;
    bool g_followAll = false;
    std::set<std::string> g_followed;
    std::map<std::string, SituLog::DrawSnapshot> g_lastSnapshot;
    std::string g_path;
    size_t g_lines = 0;

    // UTC wall clock to the millisecond. GetSystemTime is UTC already.
    std::string NowForLine()
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%02u:%02u:%02u.%03u",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        return buffer;
    }

    std::string NowForFileName()
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u-%02u%02u%02u",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

    std::vector<std::string> ListFiles(const std::string& dir)
    {
        std::vector<std::string> names;
        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA((dir + "*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) { return names; }
        do
        {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { names.push_back(data.cFileName); }
        } while (FindNextFileA(find, &data));
        FindClose(find);
        return names;
    }

    // Caller holds g_mutex.
    void WriteLocked(const std::string& line)
    {
        if (!g_enabled) { return; }
        g_file << line << '\n';
        g_file.flush();
        if (!g_file.good())
        {
            // A dead file is worse than no file: it would eat every later line silently.
            g_enabled = false;
            g_file.close();
            return;
        }
        ++g_lines;
    }
}

namespace SituLog
{
    EnableResult Enable(const std::string& dir)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        EnableResult result;

        if (g_enabled) { g_file.close(); g_enabled = false; }

        // Rotate first so the new file is never a victim.
        for (const std::string& victim : RotationVictims(ListFiles(dir), 4))
        {
            DeleteFileA((dir + victim).c_str());
        }

        CreateDirectoryA(dir.c_str(), NULL);
        g_path = dir + LogFileName(NowForFileName());
        g_file.open(g_path.c_str(), std::ios::out | std::ios::app | std::ios::binary);
        if (!g_file.is_open())
        {
            result.error = "could not open " + g_path;
            result.path = g_path;
            return result;
        }

        g_enabled = true;
        g_lines = 0;
        WriteLocked(FormatLine(NowForLine(), "CMD", "log", Fields().Add("opened", g_path)));

        result.ok = true;
        result.path = g_path;
        return result;
    }

    void Disable()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_enabled)
        {
            WriteLocked(FormatLine(NowForLine(), "CMD", "log", Fields().Add("closed", g_path).Add("lines", static_cast<int>(g_lines))));
        }
        g_enabled = false;
        g_file.close();
    }

    bool IsEnabled()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_enabled;
    }

    void Follow(const std::string& callsign)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (callsign == "ALL") { g_followAll = true; return; }
        g_followed.insert(callsign);
    }

    void Unfollow()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_followAll = false;
        g_followed.clear();
        g_lastSnapshot.clear();
    }

    bool IsFollowed(const std::string& callsign)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_followAll || g_followed.count(callsign) != 0;
    }

    bool FollowingAll()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_followAll;
    }

    void Line(const char* cat, const std::string& subject, const Fields& fields)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled) { return; }
        WriteLocked(FormatLine(NowForLine(), cat, subject, fields));
    }

    void Warn(const std::string& subject, const std::string& text)
    {
        Line("WARN", subject, Fields().Add("text", text));
    }

    void Draw(const std::string& callsign, const DrawSnapshot& now)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_enabled) { return; }
        if (!g_followAll && g_followed.count(callsign) == 0) { return; }

        auto it = g_lastSnapshot.find(callsign);
        if (it != g_lastSnapshot.end() && it->second == now) { return; }

        g_lastSnapshot[callsign] = now;
        WriteLocked(FormatLine(NowForLine(), "DRAW", callsign, SnapshotFields(now)));
    }

    std::string Status()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string status = g_enabled ? "Log on, " + g_path + ", " + std::to_string(g_lines) + " lines."
                                       : "Log off.";
        status += " Following: ";
        if (g_followAll) { status += "all"; }
        else if (g_followed.empty()) { status += "nothing"; }
        else
        {
            bool first = true;
            for (const std::string& cs : g_followed)
            {
                if (!first) { status += ", "; }
                first = false;
                status += cs;
            }
        }
        return status;
    }

    size_t LinesWritten()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_lines;
    }
}
```

- [ ] **Step 3: Add to the project**

In `VATCANSitu.vcxproj`, in the `<ItemGroup>` that contains `<ClCompile Include="SituPlugin.cpp" />`, add:

```xml
    <ClCompile Include="SituLog.cpp" />
```

In the `<ItemGroup>` that contains `<ClInclude Include="SituPlugin.h" />` (search for `ClInclude Include=`), add:

```xml
    <ClInclude Include="SituLog.h" />
    <ClInclude Include="SituLogCore.h" />
```

- [ ] **Step 4: Build the DLL**

Run the MSBuild command from Global Constraints. Expected: no `error` lines; `Debug\VATCANSitu.dll` timestamp updates. The pre-existing `warning C4566` in `SituPlugin.cpp` is fine.

- [ ] **Step 5: Re-run the unit tests**

They must still pass; `SituLogCore.h` was not changed but confirm: `50/50 checks passed`.

- [ ] **Step 6: Commit**

```
git add SituLog.h SituLog.cpp VATCANSitu.vcxproj
git commit -m "SituLog: file sink, rotation, followed set and on-change DRAW"
```

---

### Task 6: Commands

**Files:**
- Modify: `SituPlugin.h` (class declaration; find `OnCompilePrivateChat` and add the override beside it)
- Modify: `SituPlugin.cpp` (add the handler after `OnCompilePrivateChat`, around line 805)

**Interfaces:**
- Consumes: `SituLog::Enable/Disable/Follow/Unfollow/Status/IsEnabled/Line`, `SituLog::ParseLogCommand`, `wxRadar::getSituWxDir()`.
- Produces: `bool SituPlugin::OnCompileCommand(const char* sCommandLine)` override.

- [ ] **Step 1: Declare the override**

In `SituPlugin.h`, next to the existing `OnCompilePrivateChat` declaration, add:

```cpp
    // .situ log <on|off|all|none|status|CALLSIGN>. Returns true only for lines that begin
    // ".situ log", so other plugins' commands and ordinary chat pass through untouched.
    bool OnCompileCommand(const char* sCommandLine) override;
```

Add `#include "SituLog.h"` to `SituPlugin.h`'s includes if it is not reachable already.

- [ ] **Step 2: Write the handler**

Append to `SituPlugin.cpp`:

```cpp
bool SituPlugin::OnCompileCommand(const char* sCommandLine)
{
    const SituLog::LogCommand command = SituLog::ParseLogCommand(sCommandLine != nullptr ? sCommandLine : "");
    if (command.action == SituLog::LogAction::NotOurs) { return false; }

    auto say = [this](const std::string& text) {
        DisplayUserMessage("VATCAN Situ", "Log", text.c_str(), true, true, false, false, false);
    };

    // Record the command itself when the log is open. For "on" the open line covers it.
    SituLog::Line("CMD", "log", SituLog::Fields().Add("args", std::string(sCommandLine)));

    switch (command.action)
    {
    case SituLog::LogAction::On:
    {
        const SituLog::EnableResult r = SituLog::Enable(wxRadar::getSituWxDir());
        say(r.ok ? "Logging to " + r.path : "Log NOT started: " + r.error);
        break;
    }
    case SituLog::LogAction::Off:
    {
        const size_t lines = SituLog::LinesWritten();
        SituLog::Disable();
        say("Log closed, " + std::to_string(lines) + " lines.");
        break;
    }
    case SituLog::LogAction::Follow:
    {
        if (!SituLog::IsEnabled())
        {
            const SituLog::EnableResult r = SituLog::Enable(wxRadar::getSituWxDir());
            if (!r.ok) { say("Log NOT started: " + r.error); break; }
            say("Logging to " + r.path);
        }
        SituLog::Follow(command.arg);
        say("Following " + command.arg + ".");
        break;
    }
    case SituLog::LogAction::FollowAll:
    {
        if (!SituLog::IsEnabled())
        {
            const SituLog::EnableResult r = SituLog::Enable(wxRadar::getSituWxDir());
            if (!r.ok) { say("Log NOT started: " + r.error); break; }
            say("Logging to " + r.path);
        }
        SituLog::Follow("ALL");
        say("Following all aircraft - this is heavy; use .situ log none to stop.");
        break;
    }
    case SituLog::LogAction::Unfollow:
        SituLog::Unfollow();
        say("Following nothing.");
        break;
    case SituLog::LogAction::Status:
        say(SituLog::Status());
        break;
    case SituLog::LogAction::Help:
    default:
        say(".situ log on | off | status | none | all | <CALLSIGN>");
        say("on/off start and stop the file; <CALLSIGN> or all also log draw decisions; none stops following.");
        break;
    }
    return true;
}
```

Confirm `wxRadar.h` is included by `SituPlugin.cpp` (it is used elsewhere there; if not, add `#include "wxRadar.h"`).

- [ ] **Step 3: Build the DLL**

Expected: no errors.

- [ ] **Step 4: Commit**

```
git add SituPlugin.h SituPlugin.cpp
git commit -m "SituLog: .situ log commands in the EuroScope command line"
```

---

### Task 7: EVT lines in every callback

**Files:**
- Modify: `SituPlugin.cpp` at `OnFunctionCall` (~725), `OnAirportRunwayActivityChanged` (~795), `OnCompilePrivateChat` (~805)
- Modify: `CSiTRadar.cpp` at `OnClickScreenObject` (~2908), `OnButtonDownScreenObject` (~3392), `OnMoveScreenObject` (~4145), `OnFunctionCall` (~4276), `OnAsrContentLoaded` (~4439), `OnFlightPlanFlightPlanDataUpdate` (~4505), `OnFlightPlanControllerAssignedDataUpdate` (~4609), `OnFlightPlanDisconnect` (~4662), `OnDoubleClickScreenObject` (~4972), `OnAsrContentToBeSaved` (~4977), `OnControllerPositionUpdate` (~4981), `OnControllerDisconnect` (~5016), `OnFlightPlanFlightStripPushed` (~5022)
- Modify: `CSiTRadar.h` to `#include "SituLog.h"`

Line numbers drift; locate each by the function signature text.

**Interfaces:**
- Consumes: `SituLog::Line`, `SituLog::Fields`, `SituLog::Truncate`.

- [ ] **Step 1: Add two small name tables**

At the top of `CSiTRadar.cpp`, after the existing static definitions (after `SituTbs::Config CSiTRadar::tbsConfig;` near line 18), add:

```cpp
namespace
{
	// Names for the CTR_DATA_TYPE_* constants, for the CTR-DATA log line.
	const char* CtrDataTypeName(int type)
	{
		switch (type)
		{
		case CTR_DATA_TYPE_SQUAWK:              return "SQUAWK";
		case CTR_DATA_TYPE_FINAL_ALTITUDE:      return "FINAL_ALTITUDE";
		case CTR_DATA_TYPE_TEMPORARY_ALTITUDE:  return "TEMPORARY_ALTITUDE";
		case CTR_DATA_TYPE_COMMUNICATION_TYPE:  return "COMMUNICATION_TYPE";
		case CTR_DATA_TYPE_SCRATCH_PAD_STRING:  return "SCRATCH_PAD_STRING";
		case CTR_DATA_TYPE_GROUND_STATE:        return "GROUND_STATE";
		case CTR_DATA_TYPE_CLEARENCE_FLAG:      return "CLEARANCE_FLAG";
		case CTR_DATA_TYPE_DEPARTURE_SEQUENCE:  return "DEPARTURE_SEQUENCE";
		case CTR_DATA_TYPE_SPEED:               return "SPEED";
		case CTR_DATA_TYPE_MACH:                return "MACH";
		case CTR_DATA_TYPE_RATE:                return "RATE";
		case CTR_DATA_TYPE_HEADING:             return "HEADING";
		case CTR_DATA_TYPE_DIRECT_TO:           return "DIRECT_TO";
		default:                                return "OTHER";
		}
	}

	// The value the CTR_DATA type names, read back from the flight plan.
	std::string CtrDataValue(CFlightPlan& fp, int type)
	{
		CFlightPlanControllerAssignedData d = fp.GetControllerAssignedData();
		switch (type)
		{
		case CTR_DATA_TYPE_SQUAWK:              return d.GetSquawk();
		case CTR_DATA_TYPE_FINAL_ALTITUDE:      return std::to_string(d.GetFinalAltitude());
		case CTR_DATA_TYPE_TEMPORARY_ALTITUDE:  return std::to_string(d.GetClearedAltitude());
		case CTR_DATA_TYPE_COMMUNICATION_TYPE:  return std::string(1, d.GetCommunicationType());
		case CTR_DATA_TYPE_SCRATCH_PAD_STRING:  return d.GetScratchPadString();
		case CTR_DATA_TYPE_SPEED:               return std::to_string(d.GetAssignedSpeed());
		case CTR_DATA_TYPE_MACH:                return std::to_string(d.GetAssignedMach());
		case CTR_DATA_TYPE_RATE:                return std::to_string(d.GetAssignedRate());
		case CTR_DATA_TYPE_HEADING:             return std::to_string(d.GetAssignedHeading());
		case CTR_DATA_TYPE_DIRECT_TO:           return d.GetDirectToPointName();
		default:                                return "";
		}
	}

	const char* ButtonName(int button)
	{
		switch (button)
		{
		case BUTTON_LEFT:   return "L";
		case BUTTON_MIDDLE: return "M";
		case BUTTON_RIGHT:  return "R";
		default:            return "?";
		}
	}
}
```

If any accessor name above does not compile (for example `GetAssignedRate`), check `lib/EuroScopePlugIn.h` for the exact name in `CFlightPlanControllerAssignedData` and use that; do not drop the case.

- [ ] **Step 2: Insert one line at the top of each callback**

Each goes as the first statement of the function body. `CSiTRadar.cpp`:

```cpp
// OnFlightPlanFlightPlanDataUpdate
	SituLog::Line("EVT", "FP-DATA", SituLog::Fields()
		.Add("callsign", FlightPlan.GetCallsign())
		.Add("rule", FlightPlan.GetFlightPlanData().GetPlanType())
		.Add("wtc", std::string(1, FlightPlan.GetFlightPlanData().GetAircraftWtc()))
		.Add("capab", std::string(1, FlightPlan.GetFlightPlanData().GetCapibilities()))
		.Add("rmk", SituLog::Truncate(FlightPlan.GetFlightPlanData().GetRemarks(), 60))
		.Add("route", SituLog::Truncate(FlightPlan.GetFlightPlanData().GetRoute(), 60)));

// OnFlightPlanControllerAssignedDataUpdate
	SituLog::Line("EVT", "CTR-DATA", SituLog::Fields()
		.Add("callsign", FlightPlan.GetCallsign())
		.Add("type", CtrDataTypeName(DataType))
		.Add("value", CtrDataValue(FlightPlan, DataType)));

// OnFlightPlanDisconnect
	SituLog::Line("EVT", "FP-GONE", SituLog::Fields().Add("callsign", FlightPlan.GetCallsign()));

// OnFlightPlanFlightStripPushed
	SituLog::Line("EVT", "STRIP", SituLog::Fields()
		.Add("callsign", FlightPlan.GetCallsign()).Add("from", sSenderController).Add("to", sTargetController));

// OnControllerDisconnect
	SituLog::Line("EVT", "CTRL-GONE", SituLog::Fields()
		.Add("id", Controller.GetPositionId()).Add("callsign", Controller.GetCallsign()));

// OnAsrContentLoaded  (place after altFilterLow/altFilterHigh are read, at the end of the function, so the values are current)
	SituLog::Line("EVT", "ASR-LOAD", SituLog::Fields()
		.Add("loaded", Loaded)
		.Add("display", GetDataFromAsr("DisplayTypeName") != NULL ? GetDataFromAsr("DisplayTypeName") : "")
		.Add("altlow", altFilterLow).Add("althigh", altFilterHigh));

// OnAsrContentToBeSaved
	SituLog::Line("EVT", "ASR-SAVE", SituLog::Fields());

// OnFunctionCall (CSiTRadar)
	SituLog::Line("EVT", "TAG-FUNC", SituLog::Fields()
		.Add("id", FunctionId).Add("item", sItemString)
		.Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()));

// OnClickScreenObject
	SituLog::Line("EVT", "CLICK", SituLog::Fields()
		.Add("type", ObjectType).Add("id", sObjectId).Add("button", ButtonName(Button)));

// OnDoubleClickScreenObject
	SituLog::Line("EVT", "DBLCLICK", SituLog::Fields().Add("type", ObjectType).Add("id", sObjectId));

// OnButtonDownScreenObject
	SituLog::Line("EVT", "BTN-DOWN", SituLog::Fields()
		.Add("type", ObjectType).Add("id", sObjectId).Add("button", ButtonName(Button)));

// OnMoveScreenObject - only when released, not per drag pixel
	if (Released) {
		SituLog::Line("EVT", "MOVE", SituLog::Fields().Add("type", ObjectType).Add("id", sObjectId));
	}
```

`OnControllerPositionUpdate` fires every few seconds per controller. Log only first sight or a change of id or frequency. Add a static map inside the function:

```cpp
	{
		static std::map<std::string, std::string> seen;   // callsign -> "id freq"
		const std::string key = Controller.GetCallsign();
		const std::string value = std::string(Controller.GetPositionId()) + " " + std::to_string(Controller.GetPrimaryFrequency());
		if (seen[key] != value) {
			seen[key] = value;
			SituLog::Line("EVT", "CTRL", SituLog::Fields()
				.Add("callsign", key).Add("id", Controller.GetPositionId())
				.Add("freq", Controller.GetPrimaryFrequency()));
		}
	}
```

`SituPlugin.cpp`:

```cpp
// OnFunctionCall (SituPlugin) - after fp is assigned
    SituLog::Line("EVT", "TAG-FUNC", SituLog::Fields()
        .Add("id", FunctionId).Add("item", sItemString).Add("callsign", fp.IsValid() ? fp.GetCallsign() : ""));

// OnAirportRunwayActivityChanged
    SituLog::Line("EVT", "RUNWAYS", SituLog::Fields());

// OnCompilePrivateChat
    SituLog::Line("EVT", "CHAT", SituLog::Fields()
        .Add("from", sSenderCallsign).Add("len", static_cast<int>(strlen(sChatMessage))));
```

For `OnCompilePrivateChat` check the parameter name for the message text in the existing signature and use it.

In `CSiTRadar::updateActiveRunways`, after each `menuState.activeArrivalRunways.push_back(arrival);` add:

```cpp
				SituLog::Line("NET", "RUNWAYS", SituLog::Fields()
					.Add("airport", arrival.airport).Add("rwy", arrival.name).Add("course", arrival.trueCourse));
```

- [ ] **Step 3: Build the DLL**

Expected: no errors.

- [ ] **Step 4: Commit**

```
git add CSiTRadar.cpp CSiTRadar.h SituPlugin.cpp
git commit -m "SituLog: one EVT line at every EuroScope callback"
```

---

### Task 8: ES> lines at every outbound SDK call

**Files:**
- Modify: `CSiTRadar.cpp` (sites listed below; locate by the code text, not the line number)
- Modify: `CSiTRadar.h` (`ModifySFI`, `ModifyCtrlRemarks`, `SendPointOut`)
- Modify: `SituPlugin.cpp` (two `SetScratchPadString` calls near line 778 and 788)

**Interfaces:**
- Consumes: `SituLog::Line`.

- [ ] **Step 1: Correlation loop in OnRefresh**

Around the four `radarTarget.Uncorrelate()` calls and one `CorrelateWithFlightPlan` between lines ~775 and ~845, add a line after each call with the reason:

```cpp
// after: radarTarget.Uncorrelate();  inside "if (sqitr == menuState.squawkCodes.end())"
SituLog::Line("ES>", "UNCORRELATE", SituLog::Fields().Add("callsign", callSign).Add("why", "no-code-match").Add("squawk", radarTarget.GetPosition().GetSquawk()));

// after: radarTarget.CorrelateWithFlightPlan(...)
SituLog::Line("ES>", "CORRELATE", SituLog::Fields().Add("callsign", callSign).Add("squawk", radarTarget.GetPosition().GetSquawk()).Add("fp", sqitr->fpcs));

// after: radarTarget.Uncorrelate();  in the multiple discrete branch
SituLog::Line("ES>", "UNCORRELATE", SituLog::Fields().Add("callsign", callSign).Add("why", "multiple-discrete").Add("squawk", radarTarget.GetPosition().GetSquawk()));

// after: radarTarget.Uncorrelate();  in "else if (radarFlags == 0)"
SituLog::Line("ES>", "UNCORRELATE", SituLog::Fields().Add("callsign", callSign).Add("why", "no-radar"));

// after: radarTarget.Uncorrelate();  in "else if (!mAcData[callSign].autoCorrelationCleared)"
SituLog::Line("ES>", "UNCORRELATE", SituLog::Fields().Add("callsign", callSign).Add("why", "primary-only"));
```

- [ ] **Step 2: Handoff bookkeeping in OnRefresh**

```cpp
// after: ...SetFlightStripAnnotation(0, "");  (~line 1100, "Clear the entry used for pointout coordination")
SituLog::Line("ES>", "ANNOT", SituLog::Fields().Add("callsign", callSign).Add("index", 0).Add("now", "").Add("why", "handoff-started"));

// after: GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetControllerAssignedData().SetFlightStripAnnotation(1, "");  (~1120)
SituLog::Line("ES>", "ANNOT", SituLog::Fields().Add("callsign", callSign).Add("index", 1).Add("now", "").Add("why", "handoff-accepted"));
```

- [ ] **Step 3: Direct-to window handler (~3067–3134)**

```cpp
// after each SetDirectToPointName(c.c_str());  (two sites)
SituLog::Line("ES>", "DIRECT", SituLog::Fields().Add("callsign", cs).Add("fix", c));

// after AmendFlightPlan(); in the Ok branch, where rtestr is the new route
SituLog::Line("ES>", "ROUTE", SituLog::Fields().Add("callsign", cs).Add("now", SituLog::Truncate(rtestr, 60)).Add("amended", true));
```

- [ ] **Step 4: Right-click menu handlers (~3420–3600 and ~3990)**

```cpp
// after ...InitiateHandoff(...GetCoordinatedNextController()...)   ("AutoHandoff")
SituLog::Line("ES>", "HANDOFF", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()).Add("to", GetPlugIn()->FlightPlanSelectASEL().GetCoordinatedNextController()).Add("via", "auto"));

// after ...StartTracking();
SituLog::Line("ES>", "TRACK", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()));

// after ...EndTracking();
SituLog::Line("ES>", "UNTRACK", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()));

// after ...FlightPlanSelectASEL().Uncorrelate();   ("Decorrelate" menu)
SituLog::Line("ES>", "UNCORRELATE", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()).Add("why", "menu"));

// after ...SetFlightStripAnnotation(1, "");  (~3517, point-out recall)
SituLog::Line("ES>", "ANNOT", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()).Add("index", 1).Add("now", "").Add("why", "pointout-recall"));

// after ...InitiateHandoff(GetPlugIn()->ControllerSelectByPositionId(sObjectId)...)  ("ManHandoff")
SituLog::Line("ES>", "HANDOFF", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()).Add("to", sObjectId).Add("via", "menu"));

// after ...SetCommunicationType(*sObjectId);
SituLog::Line("ES>", "COMM", SituLog::Fields().Add("callsign", GetPlugIn()->FlightPlanSelectASEL().GetCallsign()).Add("type", std::string(1, *sObjectId)));

// after GetPlugIn()->FlightPlanSelect(sObjectId).AcceptHandoff();   (~3990)
SituLog::Line("ES>", "HO-ACCEPT", SituLog::Fields().Add("callsign", sObjectId));
```

Search `CSiTRadar.cpp` for any other `InitiateHandoff`, `AcceptHandoff`, `RefuseHandoff`, `StartTracking`, `EndTracking`, `SetSquawk`, `SetClearedAltitude`, `SetAssigned`, `PushFlightStrip` not covered above (for example the manual CJS window's Enter handler, `WINDOW_HANDOFF_EXT_CJS`) and add the matching line with `via` describing the origin.

- [ ] **Step 5: Scratchpad helpers in CSiTRadar.h**

In `ModifySFI`, immediately before `fp.GetControllerAssignedData().SetScratchPadString(newstring.c_str());`, capture the old value and log after:

```cpp
        const std::string was = fp.GetControllerAssignedData().GetScratchPadString();
        fp.GetControllerAssignedData().SetScratchPadString(newstring.c_str());
        fp.GetFlightPlanData().AmendFlightPlan();
        SituLog::Line("ES>", "SCRATCHPAD", SituLog::Fields().Add("callsign", fp.GetCallsign()).Add("was", was).Add("now", newstring).Add("via", "sfi"));
```

Same in `ModifyCtrlRemarks` with `.Add("via", "remarks")`. In `SendPointOut`, after `SetFlightStripAnnotation(0, message)` and `PushFlightStrip(...)`:

```cpp
        SituLog::Line("ES>", "ANNOT", SituLog::Fields().Add("callsign", fp->GetCallsign()).Add("index", 0).Add("now", message).Add("why", "pointout"));
        SituLog::Line("ES>", "STRIP-PUSH", SituLog::Fields().Add("callsign", fp->GetCallsign()).Add("to", target));
```

Use the actual parameter names in those functions (`message`, `target` or whatever the header calls them).

- [ ] **Step 6: SituPlugin.cpp scratchpad writes (~778, ~788, the IFR release functions)**

After each `SetScratchPadString(...)`:

```cpp
        SituLog::Line("ES>", "SCRATCHPAD", SituLog::Fields().Add("callsign", fp.GetCallsign()).Add("was", spString).Add("now", <the new string expression>).Add("via", "ifr-release"));
```

`spString` is already the old value in that function.

- [ ] **Step 7: Build the DLL**

Expected: no errors.

- [ ] **Step 8: Commit**

```
git add CSiTRadar.cpp CSiTRadar.h SituPlugin.cpp
git commit -m "SituLog: one ES> line beside every call that changes EuroScope"
```

---

### Task 9: NET and WARN lines

**Files:**
- Modify: `CSiTRadar.cpp` constructor (file loads, ~215–300) and `OnRefresh` async message drain
- Modify: `wxRadar.cpp` (VATSIM feed ~345–380, METAR, ATIS, RainViewer fetch functions)
- Modify: `cpdlc.cpp` (poll and send)

**Interfaces:**
- Consumes: `SituLog::Line`, `SituLog::Warn`.

- [ ] **Step 1: File loads in the CSiTRadar constructor**

TBS block: after `tbsConfig = SituTbs::Parse(tbsParsed);`:

```cpp
				SituLog::Line("NET", "SituTBS.txt", SituLog::Fields().Add("path", tbsPath).Add("found", true)
					.Add("airports", static_cast<int>(tbsConfig.airports.size()))
					.Add("rules", static_cast<int>(tbsConfig.separation.size()))
					.Add("rejected", static_cast<int>(tbsParsed.skippedLines.size() + tbsConfig.rejectedLines.size())));
```

In the `else` (file missing) branch:

```cpp
				SituLog::Line("NET", "SituTBS.txt", SituLog::Fields().Add("path", tbsPath).Add("found", false));
```

CPDLC block, after the three `Parse` calls:

```cpp
				SituLog::Line("NET", "SituCPDLC.txt", SituLog::Fields().Add("path", cpdlcPath).Add("found", true)
					.Add("stations", static_cast<int>(cpdlcStations.stations.size()))
					.Add("skipped", static_cast<int>(cpdlcStations.skippedLines.size()))
					.Add("duplicates", static_cast<int>(cpdlcStations.duplicateControllerIds.size())));
```

Check the member name for the station list in `CpdlcStations.h` and use it. In its `else` branch, `.Add("found", false)`.

Settings block (settings.txt / SituLocal.txt / settings.json migration, ~269–345): one line after loading:

```cpp
		SituLog::Line("NET", "settings", SituLog::Fields().Add("path", settingsPath)
			.Add("found", SituFiles::Exists(settingsPath)).Add("local", SituFiles::Exists(localPath))
			.Add("migrated", <the bool the block already computes for the json migration, or false>));
```

- [ ] **Step 2: WARN twins for every DisplayUserMessage**

Search `CSiTRadar.cpp`, `SituPlugin.cpp` for `DisplayUserMessage(` (about 37 sites, excluding the ones added in Task 6). Immediately after each, add:

```cpp
SituLog::Warn("<handler>", <the same text expression>);
```

where `<handler>` is the second argument of the `DisplayUserMessage` call (for example `"TBS"`, `"CPDLC"`) and the text expression is its third argument, as a `std::string`. Where the third argument is a `.c_str()` of a temporary, bind it to a local `const std::string msg = ...;` first and pass `msg` to both. For the async message drain in `OnRefresh` (search `TakeAsyncMessages`), one `Warn` per message with handler `"async"`.

- [ ] **Step 3: Worker fetches in wxRadar.cpp**

Find the VATSIM data feed parse (the block that builds `newADSB`/`newRVSM`). After the swap:

```cpp
            SituLog::Line("NET", "vatsim-feed", SituLog::Fields().Add("ok", true)
                .Add("pilots", static_cast<int>(newADSB.size())));
```

Note `newADSB` is moved by the swap; capture `static_cast<int>(newADSB.size())` into a local before the swap. In its catch / failure path: `.Add("ok", false).Add("error", <message>)`.

METAR (`parseVatsimMetar`) and ATIS (`parseVatsimATIS`): after their maps are published, `SituLog::Line("NET", "metar", Fields().Add("ok", true).Add("airports", <count>))` and the same for `"atis"`. On failure `.Add("ok", false)`.

RainViewer (`renderRadar` or the fetch that writes `0_0.png`): `SituLog::Line("NET", "rainviewer", Fields().Add("ok", <bool>).Add("tile", <ts or url>))`.

Measure duration where a fetch has a natural start and end in one function: `const auto t0 = std::chrono::steady_clock::now();` at the top and `.Add("ms", static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()))` on the line. Include `<chrono>`.

`wxRadar.cpp` must `#include "SituLog.h"`.

- [ ] **Step 4: CPDLC poll and send in cpdlc.cpp**

Find the poll function (the one the detached thread in `CSiTRadar.cpp:38` runs) and the send. After each HTTP result:

```cpp
    SituLog::Line("NET", "cpdlc-poll", SituLog::Fields().Add("ok", <bool>).Add("messages", <count>).Add("error", <text or "">));
    SituLog::Line("NET", "cpdlc-send", SituLog::Fields().Add("to", <callsign>).Add("type", <message type>).Add("ok", <bool>).Add("error", <text or "">));
```

- [ ] **Step 5: Build the DLL**

Expected: no errors.

- [ ] **Step 6: Commit**

```
git add CSiTRadar.cpp SituPlugin.cpp wxRadar.cpp cpdlc.cpp
git commit -m "SituLog: NET lines for loads and fetches, WARN twins for chat diagnostics"
```

---

### Task 10: DRAW snapshots

**Files:**
- Modify: `PPS.h` (`DrawPPS` signature and every shape branch)
- Modify: `ACTag.h`, `ACTag.cpp` (`DrawRTACTag`, `DrawNARDSTag` report the tag form)
- Modify: `CSiTRadar.cpp` both draw loops (VFR ~620–705, IFR ~1020–1190)

This task also owns the pre-existing uncommitted edit in `PPS.h` (the 1200 test added to the non-ADS-B Mode S fallback). Keep it; it is correct and this task's tests exercise that branch.

**Interfaces:**
- Consumes: `SituLog::Draw`, `SituLog::DrawSnapshot`, `SituLog::IsFollowed`.
- Produces: `static RECT CPPS::DrawPPS(CDC*, BOOL, BOOL, BOOL, BOOL, int, COLORREF, string, POINT, const char** shapeOut)`; `static const char* CACTag::DrawRTACTag(...)` and `DrawNARDSTag(...)` returning the tag form name.

- [ ] **Step 1: DrawPPS reports its shape**

Change the signature to add a trailing out parameter:

```cpp
    static RECT DrawPPS(CDC* dc, BOOL isCorrelated, BOOL isVFR, BOOL isADSB, BOOL isRVSM, int radFlag, COLORREF ppsColor, string squawk, POINT p, const char** shapeOut = nullptr)
```

At the top of the body: `const char* shape = "NONE";`. In each branch, set `shape` before drawing:

| Branch | shape |
|---|---|
| case 1 correlated triangle | `"TRIANGLE"` |
| case 1 magenta Y | `"Y"` |
| 7600/7700 filled triangle | `"TRIANGLE-FILLED"` |
| any 1200 hollow triangle (all four sites, including the fallback added earlier) | `"TRIANGLE"` |
| hexagon | `"HEXAGON"` |
| diamond with bar | `"DIAMOND"` |
| ellipse with check | `"CIRCLE-CHECK"` |
| uncorrelated asterisk | `"ASTERISK"` |
| ADS-B square | `"SQUARE"`, then `"SQUARE-BAR"` if the RVSM bar is added |
| ADS-B uncorrelated rays | `"SQUARE-RAYS"` |

Before `return prect;`: `if (shapeOut != nullptr) { *shapeOut = shape; }`.

Case 4 falls through into cases 5–7 when ADS-B; the later assignment wins, which is the shape actually left on screen. That is the intended reading.

- [ ] **Step 2: Tag routines report their form**

In `ACTag.h` change the return types:

```cpp
    static const char* DrawRTACTag(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, unordered_map<string, POINT>* tOffset);
    static const char* DrawNARDSTag(CDC* dc, CRadarScreen* rad, CRadarTarget* rt, CFlightPlan* fp, unordered_map<string, POINT>* tOffset);
```

In `DrawRTACTag`: declare `const char* form = "NONE";` before the first `if (tagType == 1 ...)`. Set `form = "ALPHA";` inside that block, `form = "BRAVO";` inside the Bravo block, and inside the uncorrelated block `form = adsbIdentity ? "UNCORR-ADSB" : "UNCORR";`. Return `form` after `dc->RestoreDC(sDC);`. In `DrawNARDSTag`: `form = "NARDS"` inside its tagType 1 block, else `"NONE"`; return it.

- [ ] **Step 3: Snapshot in the IFR draw loop**

In `CSiTRadar.cpp`'s IFR loop, change the `DrawPPS` call to capture the shape:

```cpp
					const char* ppsShape = "NONE";
					RECT prect = CPPS::DrawPPS(&dc, isCorrelated, isVFR, isADSB, isRVSM, radarTarget.GetPosition().GetRadarFlags(), ppsColor, radarTarget.GetPosition().GetSquawk(), p, &ppsShape);
```

Capture the tag form from the two `DrawRTACTag` calls: declare `const char* tagForm = "NONE";` before the `if (radarFlags != 0 && != 4)` block and assign `tagForm = CACTag::DrawRTACTag(...)` in both branches.

After the CJS block (after `vfMarker` is computed and the label drawn, before the halo), add:

```cpp
					if (SituLog::IsFollowed(callSign)) {
						SituLog::DrawSnapshot snap;
						snap.flags = radarTarget.GetPosition().GetRadarFlags();
						snap.corr = isCorrelated;
						snap.adsb = isADSB;
						snap.rvsm = isRVSM;
						snap.vfr = isVFR;
						snap.sqk = radarTarget.GetPosition().GetSquawk();
						snap.trk = GetPlugIn()->FlightPlanSelect(callSign.c_str()).GetTrackingControllerId();
						snap.tag = mAcData[callSign].tagType;
						snap.pps = ppsShape;
						snap.colour = ppsColor == C_PPS_YELLOW ? "YELLOW" : ppsColor == C_PPS_ORANGE ? "ORANGE"
							: ppsColor == C_PPS_MAGENTA ? "MAGENTA" : ppsColor == C_PPS_RED ? "RED" : "WHITE";
						snap.vf = vfMarker;
						snap.tagfn = tagForm;
						SituLog::Draw(callSign, snap);
					}
```

`IsFollowed` takes the mutex; it runs once per aircraft per frame and is the only cost when nothing is followed.

- [ ] **Step 4: Snapshot in the VFR draw loop**

Same block in the VFR (NARDS) loop, with `tagForm` from `DrawNARDSTag` (or `"NONE"` when radar flags are 0 and it is not called).

- [ ] **Step 5: Build the DLL**

Expected: no errors.

- [ ] **Step 6: Commit**

```
git add PPS.h ACTag.h ACTag.cpp CSiTRadar.cpp
git commit -m "SituLog: DRAW snapshots for followed aircraft; VFR triangle for non-ADS-B Mode S on 1200"
```

The commit message names both changes because `PPS.h` carries both.

---

### Task 11: CI, docs, build and bundle

**Files:**
- Modify: `.github/workflows/build-prerelease.yml` (the `Run standalone tests` step)
- Modify: `README.md`

- [ ] **Step 1: Add the test to CI**

After the `TagCallsignTests` pair of lines in the workflow, add:

```
        cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\ || exit /b 1
        testbin\SituLogTests.exe || exit /b 1
```

- [ ] **Step 2: Document the commands**

Add a section to `README.md` after the feature list:

```markdown
## Debug log

Type in the EuroScope command line:

    .situ log on          start writing situWx\SituDebug-<date>-<time>.log beside the DLL
    .situ log ACA123      also log every change in what is drawn for that aircraft
    .situ log all         same for every aircraft (heavy)
    .situ log none        stop following aircraft, keep logging
    .situ log status      where the file is and how many lines
    .situ log off         stop

The log records every event from EuroScope (EVT), every change the plugin makes (ES>),
file and network loads (NET), draw decisions for followed aircraft (DRAW) and anything the
plugin refused or caught (WARN). Grep by the category column or by callsign. The five newest
logs are kept. Off until asked for.
```

- [ ] **Step 3: Run the full local verification**

```
cl /nologo /EHsc /W4 /std:c++17 tests\SituLogTests.cpp /Fe:testbin\SituLogTests.exe /Fo:testbin\ && testbin\SituLogTests.exe
cl /nologo /EHsc /W4 /std:c++17 tests\TagCallsignTests.cpp /Fe:testbin\TagCallsignTests.exe /Fo:testbin\ && testbin\TagCallsignTests.exe
```
Expected: `50/50` and `32/32`, both `OK`. Then the MSBuild command: no errors.

- [ ] **Step 4: Commit**

```
git add .github/workflows/build-prerelease.yml README.md
git commit -m "SituLog: run its tests in CI and document the commands"
```

- [ ] **Step 5: Bundle for the live checklist**

Copy `Debug\VATCANSitu.dll`, `SituTBS.txt` and `SituCPDLC.txt` into a folder laid out `VATCANSitu\VATCANSitu.dll` and `VATCANSitu\situWx\*.txt`, zip it, and hand it over with the seven-step live checklist from the spec's Testing section. The live checklist is the user's to run; do not claim it passed.

---

## Self-review against the spec

- File and rotation: Task 3 and Task 5. Path via `getSituWxDir`: Task 6 passes it.
- Line format, quoting, padding, empty values, booleans: Task 1, verified by tests.
- Six commands and replies, off at startup, follow implies on: Task 2 parses, Task 6 acts. Help reply on unknown or extra words: covered.
- EVT table: Task 7 covers every row; `OnGetTagItem`, `OnOverScreenObject`, `OnRefresh` deliberately excluded as the spec says; `CTRL` filtered on change.
- ES> table: Task 8 covers correlate (with `why`), handoff, accept, track, untrack, scratchpad (`was`/`now`), annotation, strip push, direct, route, comm. `SetSquawk`, `SetClearedAltitude`, `SetAssigned*` had no call sites in the search; Task 8 step 4 tells the implementer to search and add if any exist.
- DRAW snapshot, on-change only, shape from what was painted, both display types, cleared on `none`: Task 4, Task 5 (`Draw`, `Unfollow`), Task 10.
- NET table: Task 9 covers TBS, CPDLC file, settings, feed, METAR, ATIS, RainViewer, CPDLC poll and send, runway scan (Task 7 step 2 last item).
- WARN twins: Task 9 step 2.
- Threading: single mutex in Task 5; workers call `Line` only.
- Tests: Tasks 1–4 cover formatter, parser, rotation, snapshot compare. CI: Task 11.
- Type consistency: `Fields`, `FormatLine`, `ParseLogCommand`/`LogCommand`/`LogAction`, `RotationVictims`, `IsLogFileName`, `LogFileName`, `DrawSnapshot`, `SnapshotFields`, `Truncate` are named identically in every task. `SituLog::Draw` takes `(callsign, DrawSnapshot)` in Tasks 5 and 10. `DrawPPS`'s new parameter is `const char** shapeOut` in Task 10 both where declared and called.
