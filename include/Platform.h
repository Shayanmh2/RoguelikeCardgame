#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>

// The SDL2 host for the game's blocking, synchronous control flow.
//
// Game.cpp calls UIHelper::pause() and the card pickers and expects them
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

// How long every authored pause actually holds, as a percentage: 100 plays
// the durations as written, higher is slower. Combat pacing in Settings.
void setPacePercent(int pct);
int  pacePercent();

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
// Whether the left button is down right now. takeClick() reports a press
// once and forgets it, which is what a button wants and a drag cannot use.
bool mouseDown();

// Wheel notches since the last call, positive for scrolling up. Consumed on
// read, the same way takeClick works.
int takeWheel();

// The colour the window is cleared to each frame. EnemyArt derives it from
// the current zone backdrop so the area around the scene is a dark note of
// that art rather than a flat black gutter.
void setGroundColor(SDL_Color c);

// Draw layers, composited in this order every frame:
//
//   scene    battle sprites
//   hud      the combat panel
//   <console text>
//   overlay  damage numbers, sparks
//   hand     the cards
//   modal    a full-screen panel and the dim behind it
//
// Only overlay and below can draw on top of the log text. Each is optional.
void setSceneRenderer(const std::function<void()>& fn);
void setHudRenderer(const std::function<void()>& fn);
void setOverlayRenderer(const std::function<void()>& fn);
void setHandRenderer(const std::function<void()>& fn);
void setModalRenderer(const std::function<void()>& fn);

// The window ground colour rescaled to a chosen brightness, hue preserved.
// Widgets take their surfaces from this so the whole interface re-tints with
// the zone instead of sitting in a fixed grey.
SDL_Color groundTone(int luma);

// Jolts the whole frame - text and sprites together - decaying to nothing over
// `ms`. `strength` is the peak displacement in pixels.
void shake(int ms, float strength);

// Current shake displacement. The scene renderer adds this to its own origin so
// sprites move in lockstep with the text rather than sliding against it.
void shakeOffset(int& dx, int& dy);

} // namespace Platform
