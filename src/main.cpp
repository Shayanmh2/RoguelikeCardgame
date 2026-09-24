// SDL2 entry point. Game.cpp writes ANSI text to std::cout and calls
// UIHelper and EnemyArt, which this project implements on SDL.
#include "Game.h"
#include "Audio.h"
#include "EnemyArt.h"
#include "Platform.h"
#include <iostream>

// TODO: the Emscripten target is untouched since the SDL2 port. Platform.cpp
// blocks inside frame(), which a browser main loop will not tolerate, so it
// needs emscripten_set_main_loop before it will run at all.
int main(int, char**) {
    Audio::logLaunch("start: entering Platform::init");
    if (!Platform::init("Moonstruck")) {
        Audio::logLaunch("start: Platform::init failed, quitting");
        return 1;
    }
    Audio::logLaunch("start: window and renderer are up");

    // Decode the sprite sheets before the title screen rather than during the
    // first encounter, where it showed as a multi second stall.
    EnemyArt::preload();

    Audio::init();
    Audio::playBGM(); // looks for bgm.wav / bgm.mp3 next to the exe

    Game game;
    game.run();

    Audio::stopBGM();
    Audio::shutdown();
    Platform::shutdown();
    return 0;
}
