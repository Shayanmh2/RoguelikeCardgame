#include "Audio.h"
#include <SDL_mixer.h>
#include <unordered_map>
#include <filesystem>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <vector>
#else
    #include <unistd.h>
    #include <limits.h>
#endif

namespace {
    bool audioReady = false;
    Mix_Music* currentMusic = nullptr;
    std::string currentBgmPath;
    std::unordered_map<std::string, Mix_Chunk*> sfxCache;
}

std::string Audio::exeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string path(buf);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    std::string path(buf);
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    std::string path(buf, len);
#endif
    size_t pos = path.find_last_of("\\/");
    return (pos != std::string::npos) ? path.substr(0, pos + 1) : "";
}

void Audio::init() {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) audioReady = true;
}

void Audio::shutdown() {
    if (!audioReady) return;
    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    for (auto& [name, chunk] : sfxCache) Mix_FreeChunk(chunk);
    sfxCache.clear();
    Mix_CloseAudio();
    audioReady = false;
}

static std::string resolveTrack(const std::string& base) {
    if (std::filesystem::exists(base + ".mp3")) return base + ".mp3";
    if (std::filesystem::exists(base + ".wav")) return base + ".wav";
    return "";
}

void Audio::playBGM(int segment) {
    if (!audioReady) return;
    std::string soundsDir = exeDir() + "sounds/";

    std::string path;
    if (segment > 0) path = resolveTrack(soundsDir + "bgm" + std::to_string(segment + 1));
    if (path.empty()) path = resolveTrack(soundsDir + "bgm");
    if (path.empty() || path == currentBgmPath) return; // no file, or already playing this track

    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    currentMusic = Mix_LoadMUS(path.c_str());
    if (currentMusic) {
        Mix_PlayMusic(currentMusic, -1); // loop forever, same as the terminal game's BGM
        currentBgmPath = path;
    }
}

void Audio::stopBGM() {
    if (!audioReady) return;
    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    currentBgmPath.clear();
}

void Audio::playSFX(const std::string& name) {
    if (!audioReady) return;
    auto it = sfxCache.find(name);
    Mix_Chunk* chunk = nullptr;
    if (it != sfxCache.end()) {
        chunk = it->second;
    } else {
        std::string dir = exeDir() + "sounds/";
        std::string path;
        if (std::filesystem::exists(dir + name + ".mp3")) path = dir + name + ".mp3";
        else if (std::filesystem::exists(dir + name + ".wav")) path = dir + name + ".wav";
        else { sfxCache[name] = nullptr; return; } // remember "missing" so we don't stat the disk again
        chunk = Mix_LoadWAV(path.c_str()); // despite the name, Mix_LoadWAV decodes mp3/wav/ogg alike
        sfxCache[name] = chunk;
    }
    if (chunk) Mix_PlayChannel(-1, chunk, 0);
}
