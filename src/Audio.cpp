#include "Audio.h"

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <thread>
#include <string>

static std::atomic<bool> mp3Running{false};

// Returns the directory that contains the running exe (with trailing backslash).
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

// MP3 loop thread: opens, plays (blocking), closes, repeats.
static void mp3Loop(const std::string& path) {
    while (mp3Running) {
        std::string openCmd = "open \"" + path + "\" alias bgm";
        if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) != 0) break;
        mciSendStringA("play bgm wait", nullptr, 0, nullptr); // blocks until track ends
        mciSendStringA("close bgm", nullptr, 0, nullptr);
    }
}

void Audio::playBGM() {
    std::string dir = exeDir();

    // Prefer WAV — PlaySound loops seamlessly with zero gap.
    std::string wav = dir + "bgm.wav";
    if (fileExists(wav)) {
        PlaySoundA(wav.c_str(), nullptr, SND_FILENAME | SND_LOOP | SND_ASYNC);
        return;
    }

    // Fall back to MP3 via MCI on a background thread.
    std::string mp3 = dir + "bgm.mp3";
    if (fileExists(mp3)) {
        mp3Running = true;
        std::thread(mp3Loop, mp3).detach();
    }
    // No file found — run silently.
}

void Audio::stopBGM() {
    // Stop WAV (no-op if not playing).
    PlaySoundA(nullptr, nullptr, 0);

    // Stop MP3 thread.
    mp3Running = false;
    mciSendStringA("stop bgm", nullptr, 0, nullptr);
    mciSendStringA("close bgm", nullptr, 0, nullptr);
}

#else
// Non-Windows stub — compiles but does nothing.
std::string Audio::exeDir() { return ""; }
void Audio::playBGM()  {}
void Audio::stopBGM()  {}
#endif
