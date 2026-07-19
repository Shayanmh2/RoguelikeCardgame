#pragma once
#include <string>

class Audio {
public:
    // Starts (or switches to) the background track for a 10-encounter segment:
    // segment 0 plays sounds/bgm.wav|mp3, segment N tries sounds/bgm<N+1>.wav|mp3
    // (e.g. segment 1 -> bgm2.wav) and falls back to bgm.wav|mp3 until that track
    // exists. Re-calling with the segment already playing is a no-op. Silent if
    // no file is found.
    static void playBGM(int segment = 0);
    static void stopBGM();

    // Play a one-shot WAV from the sounds/ folder. name = filename without extension.
    // e.g. Audio::playSFX("attack") plays sounds/attack.wav. Silent if file absent.
    static void playSFX(const std::string& name);

    // Directory containing the running exe (with trailing separator). Use this
    // to resolve bundled data files (config, sounds) - NOT the working directory,
    // which can differ depending on how the exe was launched.
    static std::string exeDir();
};
