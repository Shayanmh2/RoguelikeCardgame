#include "Audio.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <thread>
#include <string>

static std::atomic<bool> bgmRunning{false};

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
static void bgmLoop(const std::string& path) {
    while (bgmRunning) {
        std::string openCmd = "open \"" + path + "\" alias bgm";
        if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) break;
        mciSendStringA("play bgm wait", nullptr, 0, nullptr); // blocks until track ends
        mciSendStringA("close bgm", nullptr, 0, nullptr);
    }
}

void Audio::playBGM() {
    std::string soundsDir = exeDir() + "sounds\\";

    // Prefer WAV, fall back to MP3 — both use the MCI thread loop.
    std::string wav = soundsDir + "bgm.wav";
    std::string mp3 = soundsDir + "bgm.mp3";

    std::string bgmPath;
    if      (fileExists(wav)) bgmPath = wav;
    else if (fileExists(mp3)) bgmPath = mp3;
    else return; // no BGM file found — run silently

    bgmRunning = true;
    std::thread(bgmLoop, bgmPath).detach();
}

void Audio::stopBGM() {
    bgmRunning = false;
    mciSendStringA("stop bgm", nullptr, 0, nullptr);
    mciSendStringA("close bgm", nullptr, 0, nullptr);
}

void Audio::playSFX(const std::string& name) {
    std::string dir = exeDir() + "sounds\\";
    std::string path;
    if      (fileExists(dir + name + ".wav")) path = dir + name + ".wav";
    else if (fileExists(dir + name + ".mp3")) path = dir + name + ".mp3";
    else return; // no file found — silent

    // Close any previously playing SFX, then open and play async.
    mciSendStringA("close sfx", nullptr, 0, nullptr);
    std::string openCmd = "open \"" + path + "\" alias sfx";
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return;
    mciSendStringA("play sfx", nullptr, 0, nullptr); // non-blocking
}

#else
// Non-Windows stubs — compile cleanly but do nothing.
std::string Audio::exeDir() { return ""; }
void Audio::playBGM()                        {}
void Audio::stopBGM()                        {}
void Audio::playSFX(const std::string&)      {}
#endif
