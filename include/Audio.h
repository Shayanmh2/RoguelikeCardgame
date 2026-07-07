#pragma once
#include <string>

class Audio {
public:
    // Looks for sounds/bgm.wav (loop) or sounds/bgm.mp3 (thread loop). Silent if absent.
    static void playBGM();
    static void stopBGM();

    // Play a one-shot WAV from the sounds/ folder. name = filename without extension.
    // e.g. Audio::playSFX("attack") plays sounds/attack.wav. Silent if file absent.
    static void playSFX(const std::string& name);

    // Directory containing the running exe (with trailing separator). Use this
    // to resolve bundled data files (config, sounds) - NOT the working directory,
    // which can differ depending on how the exe was launched.
    static std::string exeDir();
};
