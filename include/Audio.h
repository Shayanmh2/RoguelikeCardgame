#pragma once
#include <string>

class Audio {
public:
    // Looks for bgm.wav (perfect loop) then bgm.mp3 (thread loop) next to the exe.
    // Silent no-op if neither file exists.
    static void playBGM();
    static void stopBGM();

private:
    static std::string exeDir();
};
