#pragma once

// Reading and writing whole small text files, in one place.
//
// Every config file this plugin owns is a few kilobytes read once at load and written
// once at exit, so there is no reason for each of them to open streams differently. The
// previous arrangement had the settings save open the file for *reading* first and skip
// the whole write if that failed - which meant a fresh install never persisted anything,
// because the file it was checking for was the one it had not written yet.

#include <fstream>
#include <sstream>
#include <string>

namespace SituFiles
{
    inline bool Exists(const std::string& path)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        return file.is_open();
    }

    // Empty when the file cannot be opened, which callers treat the same as absent.
    inline std::string Read(const std::string& path)
    {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file.is_open()) { return std::string(); }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    }

    inline bool Write(const std::string& path, const std::string& contents)
    {
        std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!file.is_open()) { return false; }

        file << contents;
        return file.good();
    }
}
