#include "pch.h"
#include "SituLog.h"

#include <windows.h>

#include <cstdio>
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
            static_cast<unsigned int>(st.wHour), static_cast<unsigned int>(st.wMinute),
            static_cast<unsigned int>(st.wSecond), static_cast<unsigned int>(st.wMilliseconds));
        return buffer;
    }

    std::string NowForFileName()
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%04u%02u%02u-%02u%02u%02u",
            static_cast<unsigned int>(st.wYear), static_cast<unsigned int>(st.wMonth),
            static_cast<unsigned int>(st.wDay), static_cast<unsigned int>(st.wHour),
            static_cast<unsigned int>(st.wMinute), static_cast<unsigned int>(st.wSecond));
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
        // A new file starts with a full baseline for every followed aircraft.
        g_lastSnapshot.clear();
        WriteLocked(FormatLine(NowForLine(), "CMD", "log", Fields().Add("opened", g_path)));

        // WriteLocked turns the log off again if that first line did not reach the disk, so
        // g_enabled - not the open succeeding - is what the caller is being told about.
        result.ok = g_enabled;
        if (!result.ok) { result.error = "could not write to " + g_path; }
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
