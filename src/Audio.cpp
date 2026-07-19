#include "Audio.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <thread>
#include <string>

static std::atomic<bool> bgmRunning{false};
// Generation guard: bumping this retires any previous BGM thread even if it is
// mid-"play wait", so a track switch can't race the old loop into reopening.
static std::atomic<int>  bgmGen{0};
static std::string       bgmCurrentPath;

// Returns the directory containing the running exe (with trailing backslash).
std::string Audio::exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
    size_t pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

static bool fileExists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// BGM thread: opens file with "bgm" MCI alias, plays (blocking), closes, repeats.
// Exits as soon as its generation is stale (a switch or stop bumped bgmGen).
static void bgmLoop(const std::string& path, int myGen) {
    while (bgmRunning && bgmGen.load() == myGen) {
        std::string openCmd = "open \"" + path + "\" alias bgm";
        if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) break;
        mciSendStringA("play bgm wait", nullptr, 0, nullptr); // blocks until track ends
        mciSendStringA("close bgm", nullptr, 0, nullptr);
    }
}

// Prefer WAV, fall back to MP3; empty string if neither exists.
static std::string resolveTrack(const std::string& base) {
    if (fileExists(base + ".wav")) return base + ".wav";
    if (fileExists(base + ".mp3")) return base + ".mp3";
    return "";
}

void Audio::playBGM(int segment) {
    std::string soundsDir = exeDir() + "sounds\\";

    // Segment 0 -> bgm, segment N -> bgmN+1 (bgm2, bgm3, ...), falling back to
    // the base bgm track until the per-segment file is dropped into sounds/.
    std::string path;
    if (segment > 0) path = resolveTrack(soundsDir + "bgm" + std::to_string(segment + 1));
    if (path.empty()) path = resolveTrack(soundsDir + "bgm");
    if (path.empty()) return; // no BGM file found - run silently

    if (bgmRunning && path == bgmCurrentPath) return; // this track is already playing

    // Retire the old loop and start the new track.
    int gen = ++bgmGen;
    mciSendStringA("stop bgm", nullptr, 0, nullptr);
    mciSendStringA("close bgm", nullptr, 0, nullptr);
    bgmCurrentPath = path;
    bgmRunning = true;
    std::thread(bgmLoop, path, gen).detach();
}

void Audio::stopBGM() {
    bgmRunning = false;
    ++bgmGen;
    bgmCurrentPath.clear();
    mciSendStringA("stop bgm", nullptr, 0, nullptr);
    mciSendStringA("close bgm", nullptr, 0, nullptr);
}

void Audio::playSFX(const std::string& name) {
    std::string dir = exeDir() + "sounds\\";
    std::string path;
    if      (fileExists(dir + name + ".wav")) path = dir + name + ".wav";
    else if (fileExists(dir + name + ".mp3")) path = dir + name + ".mp3";
    else return; // no file found - silent

    // Close any previously playing SFX, then open and play async.
    mciSendStringA("close sfx", nullptr, 0, nullptr);
    std::string openCmd = "open \"" + path + "\" alias sfx";
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return;
    mciSendStringA("play sfx", nullptr, 0, nullptr); // non-blocking
}

#else
// Non-Windows stubs - compile cleanly but do nothing.
std::string Audio::exeDir() { return ""; }
void Audio::playBGM(int)                     {}
void Audio::stopBGM()                        {}
void Audio::playSFX(const std::string&)      {}
#endif
