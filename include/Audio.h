#pragma once
#include <string>

// Backed by SDL2_mixer. Cross-platform by construction: no WinAPI calls
// anywhere in here.
class Audio {
public:
    static void init();   // call once after SDL_Init
    static void shutdown();

    static void playBGM(int segment = 0);
    // By file stem, for tracks outside the zone rotation (the ??? encounter).
    static void playBGM(const std::string& baseName);
    static void stopBGM();
    // Volumes as percentages of full, 0 being silence. Kept apart because
    // music under a long run and a combat cue want different levels.
    static void setMusicVolume(int pct);
    static void setSfxVolume(int pct);
    static void playSFX(const std::string& name);
    // The same cue at a different pitch, so the enemy side of the field can
    // answer the player with a recognisably darker version of their own sound.
    // ratio < 1 plays it slower and lower. Cached per (name, ratio).
    static void playSFXPitched(const std::string& name, float ratio);

    static std::string exeDir();
    // Where assets, config and sounds are read from: the exe's folder, or
    // Contents/Resources inside a macOS .app (data in Contents/MacOS breaks codesign).
    static std::string dataDir();
    // Where saves are written. The exe's folder, except inside a macOS .app,
    // where writing into the signed bundle would modify it after signing (and
    // an app in /Applications usually cannot write there at all).
    static std::string saveDir();
    // One line into launch.log beside the saves. A launch that dies before it
    // can draw anything still leaves a trail on the player's own machine.
    static void logLaunch(const std::string& line);
};
