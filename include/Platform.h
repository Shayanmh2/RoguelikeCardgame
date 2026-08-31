#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>

// The SDL2 host for the game's blocking, synchronous control flow.
//
// Game.cpp calls UIHelper::pause() and UIHelper::menuSelect() and expects them
// to block. Rather than restructure the game into a frame-driven state machine,
// those blocking calls pump the SDL event/render loop from the inside via
// frame() - so the window stays responsive, animations play, and the game code
// itself never has to know it isn't talking to a terminal any more.
namespace Platform {

// Default window size. There is deliberately no logical-size scaling: text
// drawn through a scaled renderer gets resampled at fractional factors (1.2x
// when a 1600x900 canvas is stretched to a 1080p screen) and turns blurry.
// Everything lays out against the real output size instead, so glyphs stay
// pixel-exact at any window size or in fullscreen.
constexpr int DEFAULT_W = 1600;
constexpr int DEFAULT_H = 900;

bool init(const char* title);
void shutdown();

SDL_Renderer* renderer();
int cellW();
int cellH();

// Current renderer output size in real pixels; both change on resize.
int screenW();
int screenH();

// Draws one frame (scene + console) and processes pending events.
void frame();

// Blocks for ms while continuing to draw frames.
void delay(int ms);

// Keys are queued by frame() so a blocking read never misses one.
enum class Key { NONE, UP, DOWN, LEFT, RIGHT, ENTER, ESCAPE, CHAR };
struct KeyEvent { Key key = Key::NONE; char ch = 0; };

KeyEvent pollKey();               // NONE if nothing queued
KeyEvent waitKey();               // pumps frames until a key arrives
void     flushKeys();

// Mouse click in logical coordinates, consumed once. Menus use this so the
// list is clickable as well as keyboard-driven.
bool takeClick(int& x, int& y);
void mousePos(int& x, int& y);

// The colour the window is cleared to each frame. EnemyArt derives it from
// the current zone backdrop so the area around the scene is a dark note of
// that art rather than a flat black gutter.
void setGroundColor(SDL_Color c);

// The battle scene draws above the console text; EnemyArt installs this.
void setSceneRenderer(const std::function<void()>& fn);

// Set when the window is closed - the game exits at the next blocking call.
bool quitRequested();

} // namespace Platform
