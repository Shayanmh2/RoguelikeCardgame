#pragma once
#include <string>

class Audio {
public:
    // Starts (or switches to) the background track for a 10-encounter segment:

    static void playBGM(int segment = 0);
    static void stopBGM();

    static void playSFX(const std::string& name);


    static std::string exeDir();
};
