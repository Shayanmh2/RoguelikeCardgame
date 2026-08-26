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
    static void stopBGM();
    static void playSFX(const std::string& name);

    static std::string exeDir();
};
