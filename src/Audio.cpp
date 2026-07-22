#include "Audio.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <string>


static std::atomic<int>  bgmGen{0};

static std::mutex        bgmPathMutex;
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


static std::string aliasFor(int gen) { return "bgm" + std::to_string(gen); }

static void bgmLoop(const std::string& path, int myGen) {
    std::string alias = aliasFor(myGen);
    std::string openCmd = "open \"" + path + "\" alias " + alias;
    if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return;
    mciSendStringA(("play " + alias).c_str(), nullptr, 0, nullptr); 

    while (bgmGen.load() == myGen) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (bgmGen.load() != myGen) break;

        char status[64] = {0};
        mciSendStringA(("status " + alias + " mode").c_str(), status, sizeof(status), nullptr);
        if (std::string(status) != "playing") {

            mciSendStringA(("close " + alias).c_str(), nullptr, 0, nullptr);
            if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) return;
            mciSendStringA(("play " + alias).c_str(), nullptr, 0, nullptr);
        }
    }

    mciSendStringA(("stop " + alias).c_str(), nullptr, 0, nullptr);
    mciSendStringA(("close " + alias).c_str(), nullptr, 0, nullptr);
}

// Prefer WAV, fall back to MP3; empty string if neither exists.
static std::string resolveTrack(const std::string& base) {
    if (fileExists(base + ".wav")) return base + ".wav";
    if (fileExists(base + ".mp3")) return base + ".mp3";
    return "";
}

void Audio::playBGM(int segment) {
    std::string soundsDir = exeDir() + "sounds\\";

    std::string path;
    if (segment > 0) path = resolveTrack(soundsDir + "bgm" + std::to_string(segment + 1));
    if (path.empty()) path = resolveTrack(soundsDir + "bgm");
    if (path.empty()) return; // no BGM file found - run silently

    {
        std::lock_guard<std::mutex> lock(bgmPathMutex);
        if (path == bgmCurrentPath) return; // this track is already playing (or bgmCurrentPath is empty and nothing is)
        bgmCurrentPath = path;
    }


    int gen = ++bgmGen;
    std::thread(bgmLoop, path, gen).detach();
}

void Audio::stopBGM() {
    std::lock_guard<std::mutex> lock(bgmPathMutex);
    bgmCurrentPath.clear();
    ++bgmGen; // claimed by nothing - any running thread notices and tears itself down
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
