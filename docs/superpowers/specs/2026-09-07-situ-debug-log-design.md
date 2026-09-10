# SituDebug: a live log of everything the plugin does with EuroScope

Date: 2026-09-07. Status: approved design, awaiting implementation plan.

## Why

Almost everything in EuroScope fails silently. A tag renders blank, a marker never draws,
a correlation is undone a frame after it was made, and nothing on screen says which of a
dozen conditions was false. Today the plugin's only diagnostic is a chat-area message at a
handful of points, and none of those cover the per-frame decisions - radar flags, correlation,
ADS-B, squawk - that decide what is drawn.

This adds a log file, written live, of every event the plugin receives from EuroScope, every
change it makes to EuroScope, every file and network load, and, for chosen aircraft, every
change in the inputs and outputs of the drawing code. It is off until asked for.

## Scope

In:

- A logger module with a file sink, thread-safe, flushed per line.
- One log line at each SDK callback the plugin implements.
- One log line beside each SDK call that changes EuroScope state.
- Per-aircraft draw-decision lines for followed callsigns, emitted on change only.
- File and network load results.
- Chat commands to control it.
- Unit tests for every pure part.

Out, deliberately:

- An in-scope viewer window. The file is the record; tail it.
- A facade that routes all outbound SDK calls through one layer. That is the follow-on
  (approach B) and will be done one call site at a time once this log exists.
- Logging on a VFR display beyond what the shared callbacks already cover. The NARDS draw
  path gets the same DRAW snapshot as the IFR path, nothing more.

## The file

Path: `situWx\SituDebug-YYYYMMDD-HHMMSS.log`, resolved with `wxRadar::getSituWxDir()` like
every other plugin file, so it sits beside the DLL. The timestamp is the moment `on` was
typed, UTC.

Rotation: at each `on`, files matching `SituDebug-*.log` in that folder are sorted by name
and all but the newest four are deleted before the new one is created, so five exist at
most.

Every line is flushed as it is written. A crash loses at most the line being written.

If the file cannot be opened, logging disables itself and one chat message says so with
the path. No retry; the next `on` tries again.

## Line format

One event per line. Four columns, then free key=value pairs:

```
HH:MM:SS.mmm CAT  SUBJECT     key=value key="value with spaces" ...
```

- Time: UTC wall clock to the millisecond.
- CAT: fixed four characters, padded. One of:
  - `EVT ` - a callback from EuroScope.
  - `ES> ` - a call the plugin made that changes EuroScope state.
  - `DRAW` - a draw-decision snapshot for a followed aircraft.
  - `NET ` - a file load or network fetch result.
  - `CMD ` - a `.situ log` command as received.
  - `WARN` - something the plugin caught, refused or could not do.
- SUBJECT: the callsign where there is one, otherwise the callback or action name, padded
  to eleven characters. For EVT and ES> the action name comes first and the callsign second
  (see examples) so both are in fixed columns.
- Pairs: `key=value`. A value containing a space, quote or equals sign is double quoted with
  internal quotes doubled. Empty values are written as `key=` so their absence is visible.
  Booleans are `0` and `1`.

Examples, each a real event this plugin can produce:

```
02:15:58.412 EVT  FP-DATA     ACA123  rule=V wtc=M capab=L rmk="STS/ADSB" route="YHZ MIILS TUSKY"
02:15:58.413 ES>  CORRELATE   ACA123  squawk=4521
02:15:58.900 EVT  CTR-DATA    ACA123  type=SQUAWK value=4521
02:15:59.001 DRAW ACA123      flags=6 corr=1 adsb=0 rvsm=0 vfr=0 sqk=4521 trk=QM tag=1 pps=HEXAGON colour=YELLOW vf=0 tagfn=ALPHA
02:16:03.220 CMD  log         args="ACA123"
02:16:10.005 NET  SituTBS.txt path="C:\...\situWx\SituTBS.txt" airports=1 rules=12 rejected=0
02:16:10.006 NET  SituCPDLC.txt found=0 path="C:\...\situWx\SituCPDLC.txt"
02:16:12.777 WARN OnGetTagItem ACA123  what="std::out_of_range" where=CPDLCMessages
02:16:20.100 ES>  HANDOFF     ACA123  to=TR
02:16:20.101 ES>  SCRATCHPAD  ACA123  was=" S DIRECT YYZ" now=" S"
```

Grep by column: `grep " ES> "` for everything the plugin changed, `grep " ACA123 "` for
one aircraft's whole story, `grep DRAW` for symbology changes.

## Commands

Handled in `SituPlugin::OnCompileCommand`. The plugin returns true for any line beginning
`.situ log` so EuroScope does not treat it as chat, and prints a one-line reply to the chat
area under handler `VATCAN Situ` / `Log`.

| Command | Effect | Reply |
|---|---|---|
| `.situ log on` | Rotate, open a new file, enable EVT/ES>/NET/CMD/WARN. | The full path. |
| `.situ log off` | Disable, close the file. Followed set is kept. | `Log closed, N lines.` |
| `.situ log <CALLSIGN>` | Add to the followed set. Enables DRAW for it. Implies `on` if off. | `Following CALLSIGN.` |
| `.situ log all` | Follow every aircraft. | `Following all aircraft - this is heavy; use .situ log none to stop.` |
| `.situ log none` | Empty the followed set. Logging otherwise continues. | `Following nothing.` |
| `.situ log status` | No change. | `Log on/off, path, lines written, following: ...` |
| anything else after `.situ log` | No change. | The table above, one line per command. |

A callsign argument is anything that is not one of the keywords. It is upper-cased. No
check that the aircraft exists: following an aircraft that has not appeared yet is the point.

Every accepted command is itself written to the log as a `CMD` line when the log is open.

Startup state: off, following nothing. Nothing is written until `on` or a callsign is typed.

## What is logged where

### EVT: callbacks from EuroScope

One line at the top of each callback the plugin implements, before any logic. If the
callback has an early return, the line is still written. Fields are what the callback
receives, not what the plugin does with it.

| Callback | SUBJECT | Fields |
|---|---|---|
| OnFlightPlanFlightPlanDataUpdate | FP-DATA | rule, wtc, capab, rmk, route (route truncated to 60 chars with `...`) |
| OnFlightPlanControllerAssignedDataUpdate | CTR-DATA | type (the CTR_DATA_TYPE name), value (the field that type names) |
| OnFlightPlanDisconnect | FP-GONE | - |
| OnFlightPlanFlightStripPushed | STRIP | from, to |
| OnControllerPositionUpdate | CTRL | id, callsign, freq (logged only on first sight and on change of id or freq, otherwise this fires every few seconds per controller) |
| OnControllerDisconnect | CTRL-GONE | id, callsign |
| OnAirportRunwayActivityChanged | RUNWAYS | then one `NET RUNWAYS` line per active arrival runway found: airport, rwy, course |
| OnAsrContentLoaded | ASR-LOAD | display (the DisplayTypeName), altlow, althigh |
| OnAsrContentToBeSaved | ASR-SAVE | - |
| OnFunctionCall | TAG-FUNC | id, item, callsign |
| OnGetTagItem | (not logged: fires per drawn tag per frame; see WARN) | |
| OnCompileCommand | CMD | args |
| OnCompilePrivateChat | CHAT | from, len |
| OnClickScreenObject | CLICK | type (the constant name where known, else number), id, button |
| OnDoubleClickScreenObject | DBLCLICK | type, id |
| OnButtonDownScreenObject | BTN-DOWN | type, id, button |
| OnMoveScreenObject | MOVE | type, id, released (logged only when released, not per drag pixel) |
| OnOverScreenObject | (not logged: fires per mouse move) | |
| OnRefresh | (not logged: fires per frame; DRAW covers it) | |

### ES>: calls that change EuroScope

One line beside each call, written after the call returns where the SDK gives a result,
so a refused action is logged as refused. Sites found by search: 20 in `CSiTRadar.cpp`,
3 in `SituPlugin.cpp`, 6 in `CSiTRadar.h` (the scratchpad helpers).

| SDK call | SUBJECT | Fields |
|---|---|---|
| CorrelateWithFlightPlan | CORRELATE | squawk, fp |
| Uncorrelate | UNCORRELATE | why (one of: no-code-match, multiple-discrete, primary-only, no-radar, menu) |
| InitiateHandoff | HANDOFF | to |
| AcceptHandoff / RefuseHandoff | HO-ACCEPT / HO-REFUSE | - |
| StartTracking / EndTracking | TRACK / UNTRACK | - |
| SetScratchPadString | SCRATCHPAD | was, now |
| SetFlightStripAnnotation | ANNOT | index, was, now |
| PushFlightStrip | STRIP-PUSH | to |
| SetDirectToPointName | DIRECT | fix |
| SetRoute + AmendFlightPlan | ROUTE | now (truncated to 60), amended=1 |
| SetSquawk | SQUAWK | code |
| SetCommunicationType | COMM | type |
| SetASELAircraft | (not logged: fires on every tag click; CLICK covers it) | |
| SetClearedAltitude / SetAssigned* | CFL / ASSIGNED | value |
| RequestRefresh | (not logged: too frequent, no state) | |

The `why` on UNCORRELATE is the one field here that is not simply the SDK argument. It is
worth it: the correlation logic runs every frame with four different reasons to break a
correlation, and "which one" is the whole question when a manual correlation vanishes.

### DRAW: draw decisions for followed aircraft

A `DrawSnapshot` struct, one per followed callsign, held in a map in the logger:

```
flags   int     radar flags
corr    bool    correlated
adsb    bool
rvsm    bool
vfr     bool    flight plan rule V
sqk     string  transponder code
trk     string  tracking controller id
tag     int     tagType
pps     string  shape drawn: NONE, TRIANGLE, TRIANGLE-FILLED, Y, HEXAGON, DIAMOND,
                CIRCLE-CHECK, ASTERISK, SQUARE, SQUARE-BAR, SQUARE-RAYS
colour  string  YELLOW, ORANGE, MAGENTA, RED, WHITE
vf      bool    VF marker decision
tagfn   string  ALPHA, BRAVO, UNCORR, UNCORR-ADSB, NARDS, NONE
```

`CPPS::DrawPPS` returns the shape name it drew (a `const char*` from a fixed table) alongside
the rect it returns now, so the shape is recorded from what was actually painted rather
than re-derived. The tag routine sets `tagfn` the same way.

At the end of each aircraft's draw, if the callsign is followed (or `all`), the snapshot is
filled and compared field by field with the stored one. If any field differs, or there is no
stored one, a DRAW line with every field is written and the snapshot stored. Comparison
cost is a map lookup per aircraft per frame when nothing is followed; the fill happens only
for followed callsigns.

When `.situ log none` empties the set, stored snapshots are cleared so the next follow
starts with a full line.

### NET: loads and fetches

| Source | Fields |
|---|---|
| SituTBS.txt | path, found, airports, rules, rejected |
| SituCPDLC.txt | path, found, stations, dcl, freetext, skipped, duplicates |
| settings.txt / SituLocal.txt / settings.json migration | path, found, migrated |
| VATSIM data feed | ok, pilots, adsb, rvsm, ms (duration) |
| METAR fetch | ok, airports, ms |
| ATIS fetch | ok, airports, ms |
| RainViewer | ok, tile, ms |
| CPDLC poll | ok, messages, ms, error |
| CPDLC send | to, type, ok, error |
| Active runway scan | airports, arrival-runways |

Worker threads write these directly. The logger takes its own mutex and nothing else, and
the workers already avoid the SDK, so this adds no new cross-thread hazard.

### WARN

Any place the plugin already catches an exception, refuses an action with a reason, or
falls back silently. The existing `DisplayUserMessage` diagnostics each get a WARN twin
with the same text, so the log holds what the chat area showed. New catch sites are not
added by this work; making the existing ones visible is enough.

## Module

`SituLog.h` / `SituLog.cpp`, namespace `SituLog`:

```
void  Enable(const std::string& dir);         // rotate, open; sets enabled on success
void  Disable();
bool  IsEnabled();
void  Follow(const std::string& callsign);    // "ALL" follows everything
void  Unfollow();                             // empties the set, clears snapshots
bool  IsFollowed(const std::string& callsign);
void  Line(const char* cat, const std::string& subject, const Fields& fields);
void  Draw(const std::string& callsign, const DrawSnapshot& now);  // compare-and-log
std::string Status();
```

`Fields` is a small vector of key/value string pairs with a builder so call sites read as
`SituLog::Line("ES>", "CORRELATE", Fields().Add("callsign", cs).Add("squawk", sq))`. The
formatter that turns time, category, subject and fields into a line is a free function with
no I/O, so it can be tested.

The command parser is a free function too: `ParseLogCommand(const std::string& line)` returns
an action enum and an argument, and never touches the logger. `OnCompileCommand` calls it
and acts on the result.

Rotation is a free function taking a list of existing file names and returning the ones to
delete, so the rule is tested without a filesystem.

Every entry point is noexcept in effect: a failure to write disables the logger and reports
once. Nothing in this module calls the SDK, allocates GDI objects, or blocks on anything but
its own mutex.

## Testing

`tests/SituLogTests.cpp`, added to the CI test block like the others:

- Formatter: column widths; quoting of space, quote and equals; empty value written as
  `key=`; booleans as 0/1; route truncation at 60 with `...`.
- Command parser: every keyword; a callsign; lower-case input upper-cased; garbage yields
  the help action; `.situ log` with nothing yields help; a line that is not `.situ log` is
  not claimed.
- Rotation: zero, four, five and nine existing files; names sort by timestamp; a file that
  does not match the pattern is never selected.
- Snapshot compare: identical snapshots produce no line; each single-field change produces
  one; first sighting produces one; `Unfollow` then follow produces one.

Live checklist, run once on a fresh EuroScope start with the built DLL:

1. `.situ log status` before anything: reports off. Rules out the command hook being dead.
2. `.situ log on`: the path printed exists and has a CMD line in it.
3. `.situ log ACA123` on a visible aircraft: exactly one DRAW line appears. Change its
   assigned squawk: exactly one CTR-DATA line and, if the code differs, one DRAW line.
4. Right-click the callsign, choose DE Corr: one CLICK, one UNCORRELATE with `why=menu`,
   one DRAW with `corr=0` and `pps=ASTERISK` (or TRIANGLE on 1200).
5. `.situ log none` then `.situ log ACA123` again: one DRAW line with the full state.
6. `.situ log off`: the file stops growing. Type `.situ log on` twice more and confirm the
   folder never holds more than five logs.
7. Rename `situWx\SituTBS.txt`, reload the plugin, `.situ log on`: a `NET SituTBS.txt
   found=0` line with the path.

Item 1 is first because it can fail loudly and alone; nothing after it is interpretable if
it fails.

## Cost

Off: one boolean test per call site, a map lookup per aircraft per frame in the draw loop.

On, following nothing: one short line per callback and per outbound call. On a busy scope
this is a few lines per second, dominated by CTR-DATA and FP-DATA from other controllers'
edits.

On, following one aircraft: plus one DRAW line per change in that aircraft's state.

On, following all: plus one DRAW line per change per aircraft. Bounded, since lines are
written on change rather than per frame, but a scope of two hundred aircraft during a
reload could write a few thousand lines in the first second. The `all` reply says so.

## Follow-on: approach B

Once the log exists, outbound SDK calls move one at a time behind a `SituEs` facade that
logs and forwards, so new call sites cannot forget the log line. Each move is its own
small commit and is not part of this work.
