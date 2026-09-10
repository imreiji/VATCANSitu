#pragma once

// SituDebug log: the file sink and process-wide state. Off until Enable() succeeds.
//
// Rules, all of which the call sites depend on:
//   - Nothing here calls the EuroScope SDK or touches GDI. Worker threads may call Line().
//   - No entry point throws. A write failure disables the log; Status() says so.
//   - Line() takes the mutex and returns at once when the log is off; the Fields argument
//     is still built by the caller. Cheap at this plugin's refresh rate, not free.
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
