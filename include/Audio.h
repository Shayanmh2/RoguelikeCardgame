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
};
