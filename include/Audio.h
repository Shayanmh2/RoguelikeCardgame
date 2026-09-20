#pragma once
#include <string>

// Same public interface as the terminal game's Audio class (RewardPool.cpp
// calls Audio::exeDir() unchanged), backed by SDL2_mixer instead of the
// Windows-only MCI API the terminal version used. Cross-platform by
// construction - no WinAPI calls anywhere in here.
class Audio {
public:
    static void init();   // call once after SDL_Init
    static void shutdown();

    static void playBGM(int segment = 0);
    // By file stem, for tracks outside the zone rotation (the ??? encounter).
    static void playBGM(const std::string& baseName);
    static void stopBGM();
    static void playSFX(const std::string& name);
    // The same cue at a different pitch, so the enemy side of the field can
    // answer the player with a recognisably darker version of their own sound.
    // ratio < 1 plays it slower and lower. Cached per (name, ratio).
    static void playSFXPitched(const std::string& name, float ratio);

    static std::string exeDir();
    // Where the game's own files (assets, config, sounds) are read from. The
    // exe's folder everywhere except inside a macOS .app, where they live in
    // Contents/Resources: Contents/MacOS is for code only, and data there
    // stops codesign sealing the bundle.
    static std::string dataDir();
    // Where saves are written. The exe's folder, except inside a macOS .app,
    // where writing into the signed bundle would modify it after signing (and
    // an app in /Applications usually cannot write there at all).
    static std::string saveDir();
    // One line into launch.log beside the saves. A launch that dies before it
    // can draw anything still leaves a trail on the player's own machine.
    static void logLaunch(const std::string& line);
};
