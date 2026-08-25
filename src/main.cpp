// SDL2 entry point.
//
// The game itself (Game.cpp) is byte-for-byte the terminal build's. It still
// writes ANSI-colored text to std::cout and calls UIHelper/EnemyArt; those
// three are what this project reimplements on SDL, so the whole game - every
// enemy, boss, the Shadow Knight, the tutorial, rest sites, rewards, save/load
// - runs unchanged on Windows, macOS and Linux with no terminal involved.
#include "Game.h"
#include "Audio.h"
#include "Platform.h"

int main(int, char**) {
    if (!Platform::init("Roguelike Cardgame")) return 1;

    Audio::init();
    Audio::playBGM(); // looks for bgm.wav / bgm.mp3 next to the exe

    Game game;
    game.run();

    Audio::stopBGM();
    Audio::shutdown();
    Platform::shutdown();
    return 0;
}
