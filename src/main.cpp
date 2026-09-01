// SDL2 entry point.
//
// The game itself (Game.cpp) is byte-for-byte the terminal build's. It still
// writes ANSI-colored text to std::cout and calls UIHelper/EnemyArt; those
// three are what this project reimplements on SDL, so the whole game - every
// enemy, boss, the Shadow Knight, the tutorial, rest sites, rewards, save/load
// - runs unchanged on Windows, macOS and Linux with no terminal involved.
#include "Game.h"
#include "Audio.h"
#include "EnemyArt.h"
#include "Platform.h"
#include <iostream>

// TODO: the Emscripten target is untouched since the SDL2 port. Platform.cpp
// blocks inside frame(), which a browser main loop will not tolerate, so it
// needs emscripten_set_main_loop before it will run at all.
int main(int, char**) {
    if (!Platform::init("Roguelike Cardgame")) return 1;

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
