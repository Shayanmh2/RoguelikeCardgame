#include "Audio.h"
#include <SDL.h>       // SDL_InitSubSystem / SDL_setenv for the driver fallback below
#include <SDL_mixer.h>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <cmath>
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
            // Either layout: Contents/Resources now, Contents/MacOS in older bundles.
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
        // directories. If it cannot be made (a read-only install, say), fall back
        // to the exe's own folder rather than failing to save at all.
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
    // Every sound is an mp3. The static build decodes without Mix_Init, but a
    // build with dynamically loaded codecs needs it, so ask for the formats.
    const int want = MIX_INIT_MP3 | MIX_INIT_OGG;
    const int got  = Mix_Init(want);
    if ((got & MIX_INIT_MP3) == 0)
        std::cerr << "Audio: mp3 decoder unavailable (" << Mix_GetError()
                  << ") - the game will be silent.\n";

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
        audioReady = true;
        Mix_ReserveChannels(1);   // channel 0 is the loop's: see startLoop()
        return;
    }
    std::cerr << "Audio: Mix_OpenAudio failed on the default driver ("
              << SDL_GetCurrentAudioDriver() << "): " << Mix_GetError() << "\n";

    // SDL tries WASAPI first on Windows, and it does fail on real machines
    // (exclusive-mode devices, odd virtual endpoints, nothing attached), so walk
    // the older backends rather than run silent. Each attempt restarts the
    // subsystem, since the driver is read at init. A user-pinned
    // SDL_AUDIODRIVER is left alone.
    if (SDL_getenv("SDL_AUDIODRIVER") != nullptr) return;

    for (const char* drv : { "directsound", "winmm", "wasapi", "dsp" }) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_setenv("SDL_AUDIODRIVER", drv, 1);
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) continue;
        if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0) {
            audioReady = true;
            Mix_ReserveChannels(1);
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

// The first of base.mp3, .ogg, .wav that exists: an mp3 put in by hand wins
// over a generated OGG, and an OGG over a WAV left behind.
static std::string resolveTrack(const std::string& base) {
    for (const char* ext : {".mp3", ".ogg", ".wav"})
        if (std::filesystem::exists(base + ext)) return base + ext;
    return "";
}

// Shared by both playBGM overloads: everything after "which file".
static void startTrack(const std::string& path);

// Stored as well as applied: a track opened later starts at the set level,
// and Mix_Volume(-1) only reaches the channels that exist right now.
static int gMusicVol = 100, gSfxVol = 100;

void Audio::setMusicVolume(int pct) {
    gMusicVol = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (audioReady) Mix_VolumeMusic(MIX_MAX_VOLUME * gMusicVol / 100);
}

// Mix_Volume(-1) averages over the open channels, dividing by their count:
// with no audio device there are none, and it divided by zero.
void Audio::setSfxVolume(int pct) {
    gSfxVol = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
    if (audioReady) Mix_Volume(-1, MIX_MAX_VOLUME * gSfxVol / 100);
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
bool Audio::playBGM(const std::string& baseName) {
    if (!audioReady) return false;
    const std::string path = resolveTrack(dataDir() + "sounds/" + baseName);
    if (path.empty()) return false;
    startTrack(path);
    return true;
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
    const std::string path = resolveTrack(Audio::dataDir() + "sounds/" + name);
    if (path.empty()) { sfxCache[name] = nullptr; return nullptr; } // remember "missing" so we don't stat the disk again
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
// Resampled with a Lanczos-3 window, its cutoff lowered when pitching up so
// nothing folds back, which is about what a browser does with playbackRate:
// nearest-sample picking left pitched cues gritty. Made once per cue and
// pitch, then cached, so the cost is paid on the first play only.
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
    const int LOBES = 3;
    const double scale = ratio > 1.0f ? 1.0 / ratio : 1.0;   // the cutoff, against the source's
    const long reach = (long)std::ceil(LOBES / scale);
    const double PI = 3.14159265358979323846;
    auto lanczos = [&](double x) {
        if (x == 0.0) return 1.0;
        if (std::fabs(x) >= LOBES) return 0.0;
        const double px = PI * x;
        return LOBES * std::sin(px) * std::sin(px / LOBES) / (px * px);
    };
    const int chans = channels < 8 ? channels : 8;
    for (Uint32 f = 0; f < outFrames; f++) {
        const double x = f * (double)ratio;
        const long centre = (long)std::floor(x);
        double acc[8] = { 0 }, weight = 0.0;
        for (long i = centre - reach + 1; i <= centre + reach; i++) {
            if (i < 0 || i >= (long)inFrames) continue;
            const double w = lanczos((x - (double)i) * scale);
            if (w == 0.0) continue;
            weight += w;
            for (int c = 0; c < chans; c++) acc[c] += w * in[i * channels + c];
        }
        for (int c = 0; c < channels; c++) {
            const double v = (c < chans && weight != 0.0) ? acc[c] / weight : 0.0;
            out[f * channels + c] = (Sint16)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : std::lround(v));
        }
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

// The loop plays on channel 0, which Mix_ReserveChannels(1) keeps out of the
// pool the one-shots draw from, so a burst of cues can never take it.
static const int LOOP_CHANNEL = 0;
static std::string gLoopName;

void Audio::startLoop(const std::string& name) {
    if (!audioReady) return;
    if (gLoopName == name && Mix_Playing(LOOP_CHANNEL)) return;
    Mix_Chunk* chunk = loadSFX(name);
    if (!chunk) return;
    Mix_HaltChannel(LOOP_CHANNEL);
    Mix_PlayChannel(LOOP_CHANNEL, chunk, -1);
    gLoopName = name;
}

// A fade the mixer will not start is a cut instead: it refuses one on a
// channel at volume 0 (the effects slider at nothing), and a loop left
// running there would be heard again the moment the slider came back up.
void Audio::stopLoop(int fadeMs) {
    if (!audioReady || gLoopName.empty()) return;
    if (fadeMs <= 0 || Mix_FadeOutChannel(LOOP_CHANNEL, fadeMs) == 0)
        Mix_HaltChannel(LOOP_CHANNEL);
    gLoopName.clear();
}

// Forgotten as the track playing, so the same one can be started again
// later; startTrack() frees it when the next one begins.
void Audio::fadeOutBGM(int ms) {
    if (!audioReady) return;
    Mix_FadeOutMusic(ms);
    currentBgmPath.clear();
}

// Menu cues (2026-10-04): soft and short, and only while the player keeps
// them on. A hover is held to one every 45 ms, so a sweep across a row of
// buttons ticks rather than rattles.
static bool gMenuSounds = true;
static Uint32 gLastHover = 0;

void Audio::setMenuSounds(bool on) { gMenuSounds = on; }

void Audio::menuHover() {
    if (!audioReady || !gMenuSounds) return;
    const Uint32 now = SDL_GetTicks();
    if (now - gLastHover < 45) return;
    gLastHover = now;
    playSFX("ui_hover");
}

void Audio::menuSelect(bool back) {
    if (!audioReady || !gMenuSounds) return;
    if (back) playSFXPitched("ui_select", 0.8f);
    else      playSFX("ui_select");
}
