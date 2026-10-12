#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <functional>

// The SDL2 host. The game's blocking calls (pause(), the card pickers) pump
// the event and render loop from inside through frame(), so the window stays
// responsive while the game code runs as if it were in a terminal.
namespace Platform {

// Default window size. No logical-size scaling: a scaled renderer resamples
// text at fractional factors and blurs it, so everything lays out against the
// real output size instead.
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

// Keys get queued by frame() so a blocking read never misses one. Controllers
// feed the same queue: d-pad/left stick are the arrows, A is Enter, B is
// Escape, X is DETAILS (a card's "+", the one thing only the mouse could do),
// R2 is TO_ACTIONS and L2 TO_CARDS. `pad` means a controller sent it.
enum class Key { NONE, UP, DOWN, LEFT, RIGHT, ENTER, ESCAPE, CHAR, DETAILS, TO_ACTIONS, TO_CARDS };
struct KeyEvent { Key key = Key::NONE; char ch = 0; bool pad = false; };

KeyEvent pollKey();               // NONE if nothing queued
void     flushKeys();

// Presses of the tip button since the last call (Tab, or a controller's Y and
// shoulders; Shift+Tab and the left shoulder count back). The combat panel
// steps through its status tips with them, so no mouse is needed to read one.
int takeTipSteps();
// Goes up with every other input: a key, a click, the mouse moving. A tip
// picked with the tip button stays up until this changes.
unsigned inputSerial();
// The pointer is hidden while a controller is in use, and the mouse's
// resting place should then raise nothing.
bool pointerHidden();

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
//   toast    achievement notices, over everything
//
// Only overlay and below can draw on top of the log text. Each is optional.
void setSceneRenderer(const std::function<void()>& fn);
void setHudRenderer(const std::function<void()>& fn);
void setOverlayRenderer(const std::function<void()>& fn);
void setHandRenderer(const std::function<void()>& fn);
// Drawn after the hand and before the pop-ups: a tooltip has to sit over the
// log and the cards, but never over a choice that is waiting for an answer.
void setTipRenderer(const std::function<void()>& fn);
void setModalRenderer(const std::function<void()>& fn);
void setToastRenderer(const std::function<void()>& fn);

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
