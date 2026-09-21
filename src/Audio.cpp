#include "Audio.h"
#include <SDL.h>       // SDL_InitSubSystem / SDL_setenv for the driver fallback below
#include <SDL_mixer.h>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cstdlib>   // getenv, for the macOS save folder
#include <cstdio>
#include <cstdio>

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

// True when this binary is the one inside Foo.app/Contents/MacOS/.
static bool inAppBundle(const std::string& exe) {
#if defined(__APPLE__)
    const std::string tail = "/Contents/MacOS/";
    return exe.size() > tail.size()
        && exe.compare(exe.size() - tail.size(), tail.size(), tail) == 0;
#else
    (void)exe;
    return false;
#endif
}

std::string Audio::dataDir() {
    static const std::string dir = [] {
        const std::string exe = exeDir();
        std::error_code ec;
        if (inAppBundle(exe)) {
            // Either layout: Contents/Resources is where the bundle puts data
            // now, Contents/MacOS is where older bundles kept it. Accepting
            // both means a binary and a bundle built at different times still
            // find the game's files instead of starting with nothing.
            const std::string res = exe.substr(0, exe.size() - 6) + "Resources/";   // "MacOS/"
            if (std::filesystem::exists(res + "assets", ec)) return res;
            if (std::filesystem::exists(exe + "assets", ec)) return exe;
            return res;   // report the intended one when neither exists
        }
        return exe;
    }();
    return dir;
}

// Appended to launch.log in the save folder, with the stream flushed each
// time: if the next step is what crashes, the line before it still survives.
void Audio::logLaunch(const std::string& line) {
    static bool first = true;
    std::error_code ec;
    std::filesystem::create_directories(saveDir(), ec);
    std::ofstream out(saveDir() + "launch.log", first ? std::ios::trunc : std::ios::app);
    if (!out.is_open()) return;
    if (first) {
        out << "Moonstruck launch log\n";
        out << "  exe:   " << exeDir() << "\n";
        out << "  data:  " << dataDir() << "\n";
        out << "  saves: " << saveDir() << "\n";
        for (const char* rel : { "assets", "assets/DejaVuSansMono.ttf", "config/cards.json", "sounds" })
            out << "  " << (std::filesystem::exists(dataDir() + rel, ec) ? "found   " : "MISSING ")
                << rel << "\n";
        first = false;
    }
    out << line << std::endl;
}

// Everything the game writes for itself. Named here so the move below and
// the folder itself agree on what belongs in it.
static const char* const SAVE_FILES[] = {
    "save1.dat", "save2.dat", "save3.dat", "save.dat",   // save.dat: the pre-slots file
    "progress.dat", "winrun.dat", "settings.cfg", "launch.log",
};

std::string Audio::saveDir() {
    static const std::string dir = [] {
        const std::string exe = exeDir();
        if (inAppBundle(exe)) {
            if (const char* home = std::getenv("HOME")) {
                const std::string d = std::string(home) + "/Library/Application Support/Moonstruck/";
                std::error_code ec;
                std::filesystem::create_directories(d, ec);
                if (!ec) return d;
            }
        }
        // Its own folder, rather than loose beside the exe and the asset
        // directories. If it cannot be made (a read-only install, say), fall
        // back to the old location rather than failing to save at all.
        const std::string d = exe + "saves/";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        if (ec) return exe;

        // An install that saved beside the exe keeps its slots: each file is
        // moved in once, and only if the new folder does not already have it.
        for (const char* name : SAVE_FILES) {
            const std::string from = exe + name, to = d + name;
            std::error_code e1, e2;
            if (!std::filesystem::exists(from, e1) || std::filesystem::exists(to, e2)) continue;
            std::filesystem::rename(from, to, e1);
            if (e1) {   // across a device boundary rename fails; copy and drop
                std::filesystem::copy_file(from, to, e2);
                if (!e2) std::filesystem::remove(from, e2);
            }
        }
        return d;
    }();
    return dir;
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

// Stored as well as applied: a track opened later starts at the set level,
// and Mix_Volume(-1) only reaches the channels that exist right now.
static int gMusicVol = 100, gSfxVol = 100;

void Audio::setMusicVolume(int pct) {
    gMusicVol = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    Mix_VolumeMusic(MIX_MAX_VOLUME * gMusicVol / 100);
}

void Audio::setSfxVolume(int pct) {
    gSfxVol = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    Mix_Volume(-1, MIX_MAX_VOLUME * gSfxVol / 100);
}

void Audio::playBGM(int segment) {
    if (!audioReady) return;
    std::string soundsDir = dataDir() + "sounds/";

    std::string path;
    if (segment > 0) path = resolveTrack(soundsDir + "bgm" + std::to_string(segment + 1));
    if (path.empty()) path = resolveTrack(soundsDir + "bgm");
    startTrack(path);
}

// A named track, for the fights that are not on the zone schedule.
void Audio::playBGM(const std::string& baseName) {
    if (!audioReady) return;
    startTrack(resolveTrack(dataDir() + "sounds/" + baseName));
}

static void startTrack(const std::string& path) {
    if (path.empty() || path == currentBgmPath) return; // no file, or already playing

    Mix_HaltMusic();
    if (currentMusic) { Mix_FreeMusic(currentMusic); currentMusic = nullptr; }
    currentMusic = Mix_LoadMUS(path.c_str());
    if (currentMusic) {
        Mix_VolumeMusic(MIX_MAX_VOLUME * gMusicVol / 100);
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

// Loads (once) and returns the chunk for a cue, or nullptr if there is no file.
static Mix_Chunk* loadSFX(const std::string& name) {
    auto it = sfxCache.find(name);
    if (it != sfxCache.end()) return it->second;
    std::string dir = Audio::dataDir() + "sounds/";
    std::string path;
    if (std::filesystem::exists(dir + name + ".mp3")) path = dir + name + ".mp3";
    else if (std::filesystem::exists(dir + name + ".wav")) path = dir + name + ".wav";
    else { sfxCache[name] = nullptr; return nullptr; } // remember "missing" so we don't stat the disk again
    Mix_Chunk* chunk = Mix_LoadWAV(path.c_str()); // despite the name, Mix_LoadWAV decodes mp3/wav/ogg alike
    if (!chunk)
        std::cerr << "Audio: could not load " << path << ": " << Mix_GetError() << "\n";
    sfxCache[name] = chunk;
    return chunk;
}

void Audio::playSFX(const std::string& name) {
    if (!audioReady) return;
    Mix_Chunk* chunk = loadSFX(name);
    if (chunk) Mix_PlayChannel(-1, chunk, 0);
}

// A pitched copy of a cue: SDL_mixer has no pitch control, and a second
// recording of every sound is a lot of megabytes for one bit of information.
// Nearest-sample resampling is enough for cues this short and this noisy.
void Audio::playSFXPitched(const std::string& name, float ratio) {
    if (!audioReady) return;
    if (ratio <= 0.05f || ratio > 4.0f) ratio = 1.0f;
    char key[64];
    std::snprintf(key, sizeof(key), "%s@%.2f", name.c_str(), (double)ratio);
    auto it = sfxCache.find(key);
    if (it != sfxCache.end()) {
        if (it->second) Mix_PlayChannel(-1, it->second, 0);
        return;
    }
    Mix_Chunk* base = loadSFX(name);
    // One decoded format; anything but signed 16-bit plays at its own pitch.
    int freq = 0, channels = 0; Uint16 fmt = 0;
    if (!base || !Mix_QuerySpec(&freq, &fmt, &channels) || fmt != AUDIO_S16SYS) {
        sfxCache[key] = nullptr;
        if (base) Mix_PlayChannel(-1, base, 0);
        return;
    }
    const int frameBytes = 2 * channels;
    const Uint32 inFrames = base->alen / frameBytes;
    const Uint32 outFrames = (Uint32)(inFrames / ratio);
    if (inFrames == 0 || outFrames == 0) { sfxCache[key] = nullptr; return; }

    Uint8* buf = (Uint8*)SDL_malloc((size_t)outFrames * frameBytes);
    if (!buf) { sfxCache[key] = nullptr; return; }
    const Sint16* in = (const Sint16*)base->abuf;
    Sint16* out = (Sint16*)buf;
    for (Uint32 f = 0; f < outFrames; f++) {
        Uint32 src = (Uint32)(f * ratio);
        if (src >= inFrames) src = inFrames - 1;
        for (int c = 0; c < channels; c++)
            out[f * channels + c] = in[src * channels + c];
    }
    Mix_Chunk* pitched = (Mix_Chunk*)SDL_malloc(sizeof(Mix_Chunk));
    if (!pitched) { SDL_free(buf); sfxCache[key] = nullptr; return; }
    pitched->allocated = 1;          // Mix_FreeChunk owns the buffer from here
    pitched->abuf = buf;
    pitched->alen = outFrames * frameBytes;
    pitched->volume = base->volume;
    sfxCache[key] = pitched;
    Mix_PlayChannel(-1, pitched, 0);
}
