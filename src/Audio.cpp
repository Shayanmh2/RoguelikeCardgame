#include "Audio.h"
#include <SDL.h>       // SDL_InitSubSystem / SDL_setenv for the driver fallback below
#include <SDL_mixer.h>
#include <unordered_map>
#include <filesystem>
#include <iostream>
#include <cstdint>

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
    // Every sound this game ships is an mp3. This static build has minimp3
    // compiled in, so decoding measurably works without Mix_Init - but that is a
    // property of how SDL2_mixer happens to be configured here, not a guarantee.
    // A build that loads its codecs dynamically needs this call, so ask for the
    // formats explicitly and report if one is genuinely unavailable.
    const int want = MIX_INIT_MP3 | MIX_INIT_OGG;
    const int got  = Mix_Init(want);
    if ((got & MIX_INIT_MP3) == 0)
        std::cerr << "Audio: mp3 decoder unavailable (" << Mix_GetError()
                  << ") - the game will be silent.\n";

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
        audioReady = true;
        return;
    }
    std::cerr << "Audio: Mix_OpenAudio failed on the default driver ("
              << SDL_GetCurrentAudioDriver() << "): " << Mix_GetError() << "\n";

    // SDL picks WASAPI first on Windows and it does fail on real machines:
    // exclusive-mode devices, odd virtual endpoints, no endpoint attached.
    // Older backends usually still reach the device, so walk them rather than
    // run silent. Each attempt tears the subsystem down and back up, since the
    // driver is read from the environment at init.
    //
    // If the user pinned SDL_AUDIODRIVER themselves, leave it.
    if (SDL_getenv("SDL_AUDIODRIVER") != nullptr) return;

    for (const char* drv : { "directsound", "winmm", "wasapi", "dsp" }) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_setenv("SDL_AUDIODRIVER", drv, 1);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) continue;
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
            audioReady = true;
            std::cerr << "Audio: recovered on the " << drv << " driver.\n";
            return;
        }
    }

    // Leave the environment as we found it so nothing downstream inherits a
    // backend that just failed.
    SDL_setenv("SDL_AUDIODRIVER", "", 1);
    std::cerr << "Audio: no working audio driver; the game will be silent.\n";
}

void Audio::shutdown() {
    if (!audioReady) return;
    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    for (auto& [name, chunk] : sfxCache) Mix_FreeChunk(chunk);
    sfxCache.clear();
    Mix_CloseAudio();
    Mix_Quit(); // pairs with Mix_Init
    audioReady = false;
}

static std::string resolveTrack(const std::string& base) {
    if (std::filesystem::exists(base + ".mp3")) return base + ".mp3";
    if (std::filesystem::exists(base + ".wav")) return base + ".wav";
    return "";
}

// Shared by both playBGM overloads: everything after "which file".
static void startTrack(const std::string& path);

void Audio::playBGM(int segment) {
    if (!audioReady) return;
    std::string soundsDir = exeDir() + "sounds/";

    std::string path;
    if (segment > 0) path = resolveTrack(soundsDir + "bgm" + std::to_string(segment + 1));
    if (path.empty()) path = resolveTrack(soundsDir + "bgm");
    startTrack(path);
}

// A named track, for the fights that are not on the zone schedule.
void Audio::playBGM(const std::string& baseName) {
    if (!audioReady) return;
    startTrack(resolveTrack(exeDir() + "sounds/" + baseName));
}

static void startTrack(const std::string& path) {
    if (path.empty() || path == currentBgmPath) return; // no file, or already playing

    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    currentMusic = Mix_LoadMUS(path.c_str());
    if (currentMusic) {
        Mix_PlayMusic(currentMusic, -1); // loop forever, same as the terminal game's BGM
        currentBgmPath = path;
    } else {
        // The file is on disk (resolveTrack just stat'd it), so this is a decode
        // problem, not a missing asset. Worth saying out loud either way.
        std::cerr << "Audio: could not load " << path << ": " << Mix_GetError() << "\n";
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
        if (!chunk)
            std::cerr << "Audio: could not load " << path << ": " << Mix_GetError() << "\n";
        sfxCache[name] = chunk;
    }
    if (chunk) Mix_PlayChannel(-1, chunk, 0);
}
