#include "Platform.h"
#include "Console.h"
#include "Audio.h"

#include "stb_image.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace Platform {
namespace {

SDL_Window*   gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;
TTF_Font*     gFont = nullptr;
TTF_Font*     gFontBold = nullptr;
TTF_Font*     gFontItalic = nullptr;   // the regular face, slanted (Console::setItalicFont)
int gCellW = 9, gCellH = 20;

std::deque<KeyEvent> gKeys;
bool gHasClick = false;
bool gMouseDown = false;
int  gWheel = 0;              // accumulated wheel notches, consumed by takeWheel
int  gClickX = 0, gClickY = 0;
int  gMouseX = 0, gMouseY = 0;
bool gQuit = false;

std::function<void()> gSceneRenderer;
std::function<void()> gOverlayRenderer;
std::function<void()> gTipRenderer;
std::function<void()> gHandRenderer;
std::function<void()> gHudRenderer;
std::function<void()> gModalRenderer;
std::function<void()> gToastRenderer;
SDL_Color gGround{ 13, 13, 15, 255 };

// Font sizing. The point size isn't fixed: a short window doesn't have enough
// rows for the combat text and a decent scene, so the font gets refitted
// whenever the window height changes.
std::string gFontPath, gFontBoldPath;
int gFontPxIdeal = 19;      // the DPI-correct size; never exceeded
int gFontPx = 0;            // what is currently loaded
int gLastFitH = -1;         // output height the current fit was computed for
TTF_Font* gFontBig = nullptr;
TTF_Font* gFontBigBold = nullptr;
TTF_Font* gFontSmall = nullptr;
TTF_Font* gFontDisp = nullptr;
TTF_Font* gFontTitle = nullptr;

Uint32 gShakeStart = 0, gShakeMs = 0;
float  gShakeMag = 0.0f;
int    gShakeX = 0, gShakeY = 0;

// Worked out once per frame so everything in that frame agrees on the offset.
// It falls off quadratically, so it feels like an impact settling instead of
// a vibration just stopping.
void stepShake() {
    if (gShakeMs == 0) { gShakeX = gShakeY = 0; return; }
    Uint32 t = SDL_GetTicks() - gShakeStart;
    if (t >= gShakeMs) { gShakeMs = 0; gShakeX = gShakeY = 0; return; }
    float k = 1.0f - (float)t / (float)gShakeMs;
    k *= k;
    float m = gShakeMag * k;
    gShakeX = (int)(((rand() % 2001) / 1000.0f - 1.0f) * m);
    gShakeY = (int)(((rand() % 2001) / 1000.0f - 1.0f) * m * 0.62f);
}

Uint32 gLastFrame = 0;

int      gTipSteps = 0;          // tip button presses not yet taken
unsigned gSerial = 0;            // every other input, counted
bool     gPointerHidden = false;

// --- controllers ------------------------------------------------------------
// Any number can be plugged in and any of them drives the game. They feed the
// key queue, so no screen has to know controllers exist.
std::vector<SDL_GameController*> gPads;

// A direction held on the d-pad or the left stick repeats the way a held
// arrow key does: once straight away, then again after a pause, then steadily.
constexpr Uint32 REPEAT_DELAY = 360, REPEAT_EVERY = 110;
// How far a stick has to lean to count, and how far back it has to come to
// let go: the gap keeps a stick resting near the line from flickering.
constexpr int STICK_ON = 16000, STICK_OFF = 9000;
Key    gHeldKey = Key::NONE;
int    gHeldFrom = 0;            // 1 the d-pad, 2 the left stick
Uint32 gHeldNext = 0;
Key    gStickDir = Key::NONE;
int    gScrollY = 0;             // the right stick, which scrolls like the wheel
Uint32 gScrollNext = 0;
// The triggers are axes: pressed past STICK_ON, let go under STICK_OFF.
bool   gTriggerDown[2] = { false, false };   // L2, R2

void padPress(Key k) {
    KeyEvent e;
    e.key = k;
    e.pad = true;
    gKeys.push_back(e);
}
void padHold(Key k, int from) {
    padPress(k);
    gHeldKey = k;
    gHeldFrom = from;
    gHeldNext = SDL_GetTicks() + REPEAT_DELAY;
}
void padRelease(int from, Key k = Key::NONE) {
    if (gHeldFrom == from && (k == Key::NONE || k == gHeldKey)) { gHeldKey = Key::NONE; gHeldFrom = 0; }
}
// Using a controller hides the pointer, so it stops raising tips and
// highlights wherever it was left; moving the mouse brings it back.
void padUsed() {
    if (!gPointerHidden) { gPointerHidden = true; SDL_ShowCursor(SDL_DISABLE); }
}

Key dpadKey(Uint8 button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return Key::UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return Key::DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return Key::LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return Key::RIGHT;
        default:                               return Key::NONE;
    }
}

// The left stick's direction now: whichever axis leans furthest, and the
// one it was already on until it comes back past STICK_OFF.
Key stickDir(SDL_GameController* c) {
    const int x = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    const int y = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    Key d = Key::NONE;
    if (std::abs(x) > std::abs(y)) { if (x > STICK_ON) d = Key::RIGHT; else if (x < -STICK_ON) d = Key::LEFT; }
    else                           { if (y > STICK_ON) d = Key::DOWN;  else if (y < -STICK_ON) d = Key::UP; }
    if (d == Key::NONE && gStickDir != Key::NONE) {
        const bool across = gStickDir == Key::LEFT || gStickDir == Key::RIGHT;
        if (std::abs(across ? x : y) > STICK_OFF) d = gStickDir;
    }
    return d;
}

void padButton(const SDL_ControllerButtonEvent& b) {
    padUsed();
    if (b.button == SDL_CONTROLLER_BUTTON_Y || b.button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) { gTipSteps++; return; }
    if (b.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) { gTipSteps--; return; }
    gSerial++;
    const Key d = dpadKey(b.button);
    if (d != Key::NONE) { padHold(d, 1); return; }
    switch (b.button) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START: padPress(Key::ENTER); break;
        case SDL_CONTROLLER_BUTTON_B:     padPress(Key::ESCAPE); break;
        case SDL_CONTROLLER_BUTTON_X:     padPress(Key::DETAILS); break;
        default: break;
    }
}

// Once a frame: a held direction repeats and the right stick scrolls. Nothing
// piles up while the game is busy, a repeat waits until the queue's been read.
void padTick() {
    const Uint32 now = SDL_GetTicks();
    if (gHeldKey != Key::NONE && (Sint32)(now - gHeldNext) >= 0) {
        if (gKeys.size() < 2) padPress(gHeldKey);
        gHeldNext = now + REPEAT_EVERY;
    }
    if (std::abs(gScrollY) > STICK_ON) {
        if ((Sint32)(now - gScrollNext) >= 0) { gWheel += gScrollY < 0 ? 1 : -1; gScrollNext = now + 90; }
    } else {
        gScrollNext = now;
    }
}

void pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                gQuit = true;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* c = SDL_GameControllerOpen(ev.cdevice.which)) {
                    gPads.push_back(c);
                    Audio::logLaunch(std::string("controller: ") + (SDL_GameControllerName(c) ? SDL_GameControllerName(c) : "unnamed"));
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (SDL_GameController* c = SDL_GameControllerFromInstanceID(ev.cdevice.which)) {
                    gPads.erase(std::remove(gPads.begin(), gPads.end(), c), gPads.end());
                    SDL_GameControllerClose(c);
                    gHeldKey = gStickDir = Key::NONE;
                    gHeldFrom = 0;
                    gScrollY = 0;
                    gTriggerDown[0] = gTriggerDown[1] = false;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                padButton(ev.cbutton);
                break;
            case SDL_CONTROLLERBUTTONUP:
                padRelease(1, dpadKey(ev.cbutton.button));
                break;
            case SDL_CONTROLLERAXISMOTION: {
                SDL_GameController* c = SDL_GameControllerFromInstanceID(ev.caxis.which);
                if (!c) break;
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    const Key d = stickDir(c);
                    if (d != gStickDir) {
                        gStickDir = d;
                        if (d == Key::NONE) padRelease(2);
                        else { padUsed(); gSerial++; padHold(d, 2); }
                    }
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                    gScrollY = ev.caxis.value;
                    if (std::abs(gScrollY) > STICK_ON) padUsed();
                } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT
                           || ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    // R2 to the buttons next to the cards, L2 back to the cards. Tested on a PS4 pad.
                    const int t = ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1 : 0;
                    if (!gTriggerDown[t] && ev.caxis.value > STICK_ON) {
                        gTriggerDown[t] = true;
                        padUsed();
                        gSerial++;
                        padPress(t ? Key::TO_ACTIONS : Key::TO_CARDS);
                    } else if (gTriggerDown[t] && ev.caxis.value < STICK_OFF) {
                        gTriggerDown[t] = false;
                    }
                }
                break;
            }
            case SDL_KEYDOWN: {
                KeyEvent k;
                if (ev.key.keysym.sym == SDLK_TAB) {
                    gTipSteps += (ev.key.keysym.mod & KMOD_SHIFT) ? -1 : 1;
                    break;
                }
                gSerial++;
                switch (ev.key.keysym.sym) {
                    case SDLK_UP:     k.key = Key::UP; break;
                    case SDLK_DOWN:   k.key = Key::DOWN; break;
                    case SDLK_LEFT:   k.key = Key::LEFT; break;
                    case SDLK_RIGHT:  k.key = Key::RIGHT; break;
                    case SDLK_RETURN:
                    case SDLK_KP_ENTER:
                    case SDLK_SPACE:  k.key = Key::ENTER; break;
                    case SDLK_ESCAPE: k.key = Key::ESCAPE; break;
                    case SDLK_F11:
                        // Borderless-desktop fullscreen toggle; logical-size
                        // scaling handles the rest.
                        SDL_SetWindowFullscreen(gWindow,
                            (SDL_GetWindowFlags(gWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                    default:
                        if (ev.key.keysym.sym >= 32 && ev.key.keysym.sym < 127) {
                            k.key = Key::CHAR;
                            k.ch = (char)ev.key.keysym.sym;
                        }
                        break;
                }
                if (k.key != Key::NONE) gKeys.push_back(k);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    // Already logical coordinates: SDL's renderer event watch
                    // rewrites mouse events once a logical size is set.
                    gHasClick = true;
                    gMouseDown = true;
                    gClickX = ev.button.x;
                    gClickY = ev.button.y;
                    gSerial++;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (ev.button.button == SDL_BUTTON_LEFT) gMouseDown = false;
                break;
            case SDL_MOUSEMOTION:
                gMouseX = ev.motion.x;
                gMouseY = ev.motion.y;
                gSerial++;
                if (gPointerHidden) { gPointerHidden = false; SDL_ShowCursor(SDL_ENABLE); }
                break;
            case SDL_MOUSEWHEEL:
                // Accumulated rather than latched: a fast flick delivers
                // several events between frames and all of them should count.
                gWheel += (ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                              ? -ev.wheel.y : ev.wheel.y;
                break;
            default: break;
        }
    }
    padTick();
}

// ahhh this one is annoying. Closing the window has to work even though the
// game is several blocking calls deep inside Game::run(), and threading a quit
// check through every loop in Game.cpp was never going to happen. So we just
// shut down cleanly right here instead.
void exitIfQuit() {
    if (!gQuit) return;
    Audio::stopBGM();
    Console::shutdown();
    Audio::shutdown();
    if (gFont) TTF_CloseFont(gFont);
    if (gFontBold) TTF_CloseFont(gFontBold);
    if (gFontItalic) TTF_CloseFont(gFontItalic);
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    TTF_Quit();
    SDL_Quit();
    std::exit(0);
}

// Rows the battle screen wants: EnemyArt keeps 27 for the header, status and
// card menu, and the scene gets the rest. Ask for enough that the scene gets a
// dozen rows, not its 3x minimum.
constexpr int TARGET_ROWS = 39;

bool openFontsAt(int px) {
    if (px == gFontPx && gFont) return true;
    TTF_Font* f = TTF_OpenFont(gFontPath.c_str(), px);
    if (!f) return false;
    TTF_Font* b = TTF_OpenFont(gFontBoldPath.c_str(), px);
    TTF_SetFontHinting(f, TTF_HINTING_LIGHT);
    if (b) TTF_SetFontHinting(b, TTF_HINTING_LIGHT);

    TTF_Font* oldR = gFont;
    TTF_Font* oldB = gFontBold;
    gFont = f; gFontBold = b ? b : f;
    if (oldB && oldB != oldR) TTF_CloseFont(oldB);
    if (oldR) TTF_CloseFont(oldR);

    TTF_SizeUTF8(gFont, "M", &gCellW, &gCellH);
    if (gCellW <= 0) gCellW = 9;
    gCellH = TTF_FontLineSkip(gFont);
    if (gCellH <= 0) gCellH = 20;
    gFontPx = px;
    Console::setFont(gFont, gFontBold, gCellW, gCellH);

    // Italic, for the camps' letters: the regular face slanted, so no third
    // font file ships for a few paragraphs.
    Console::setItalicFont(nullptr);
    if (gFontItalic) TTF_CloseFont(gFontItalic);
    gFontItalic = TTF_OpenFont(gFontPath.c_str(), px);
    if (gFontItalic) {
        TTF_SetFontHinting(gFontItalic, TTF_HINTING_LIGHT);
        TTF_SetFontStyle(gFontItalic, TTF_STYLE_ITALIC);
    }
    Console::setItalicFont(gFontItalic);

    // Card-text face, four fifths of the grid, for a card brief too long for
    // its card at the grid size. Never under 11px, past which it is hard to read.
    const int smallPx = std::max(11, (int)(px * 0.8f + 0.5f));
    if (TTF_Font* sf = TTF_OpenFont(gFontPath.c_str(), smallPx)) {
        TTF_SetFontHinting(sf, TTF_HINTING_LIGHT);
        TTF_Font* oldSmall = gFontSmall;
        gFontSmall = sf;
        int sw = 0, sh = 0;
        TTF_SizeUTF8(gFontSmall, "M", &sw, &sh);
        Console::setSmallFont(gFontSmall, sw > 0 ? sw : smallPx / 2, TTF_FontLineSkip(gFontSmall));
        if (oldSmall) TTF_CloseFont(oldSmall);
    }

    // Widget face, ~1.9x the grid. Card titles are sized against the card, not
    // the text grid, so they need their own point size rather than a scale-up.
    const int bigPx = (int)(px * 1.35f + 0.5f);
    TTF_Font* bf  = TTF_OpenFont(gFontPath.c_str(), bigPx);
    TTF_Font* bfb = TTF_OpenFont(gFontBoldPath.c_str(), bigPx);
    if (bf) {
        TTF_SetFontHinting(bf, TTF_HINTING_LIGHT);
        if (bfb) TTF_SetFontHinting(bfb, TTF_HINTING_LIGHT);
        TTF_Font* oldBig = gFontBig; TTF_Font* oldBigBold = gFontBigBold;
        gFontBig = bf; gFontBigBold = bfb ? bfb : bf;
        int bw = 0, bh = 0;
        TTF_SizeUTF8(gFontBig, "M", &bw, &bh);
        Console::setBigFont(gFontBig, gFontBigBold, bw > 0 ? bw : bigPx/2,
                            TTF_FontLineSkip(gFontBig));

        // Title font for the title screen and the headline overlay. Sized off the grid
        // font, then capped so an 18 character headline fits; an advance is about
        // 0.6 of the point size.
        const int byHeight = (int)(px * 5.6f + 0.5f);
        const int byWidth  = (screenW() - 44) * 10 / (18 * 6);
        const int dispPx = std::max(px, std::min(byHeight, byWidth));
        TTF_Font* df = TTF_OpenFont(gFontBoldPath.c_str(), dispPx);
        if (df) {
            TTF_SetFontHinting(df, TTF_HINTING_LIGHT);
            TTF_Font* oldDisp = gFontDisp;
            gFontDisp = df;
            int dw = 0, dh = 0;
            TTF_SizeUTF8(gFontDisp, "M", &dw, &dh);
            Console::setDisplayFont(gFontDisp, dw > 0 ? dw : dispPx/2, TTF_FontLineSkip(gFontDisp));
            if (oldDisp) TTF_CloseFont(oldDisp);
        }
        // The wordmark's face: sized for eleven characters, and loaded from
        // assets/title.ttf when that file exists.
        {
            // 6.5, not 9: at nine times the grid font the wordmark filled the
            // upper third of the window and read as a splash screen rather
            // than a title. Still well clear of the 5.6 the headline face uses.
            const int byHeight = (int)(px * 6.5f + 0.5f);
            // 7, not 6: the wordmark is drawn with tracking between the
            // letters, and a cap that ignored it let a narrow window pick a
            // size that then overflowed and dropped to the widget face.
            const int byWidth  = (screenW() - 80) * 10 / (11 * 7);
            const int titlePx  = std::max(px, std::min(byHeight, byWidth));
            std::string face = Audio::dataDir() + "assets/title.ttf";
            std::error_code ec;
            if (!std::filesystem::exists(face, ec)) face = gFontBoldPath;
            TTF_Font* tf = TTF_OpenFont(face.c_str(), titlePx);
            if (!tf && face != gFontBoldPath) tf = TTF_OpenFont(gFontBoldPath.c_str(), titlePx);
            if (tf) {
                TTF_SetFontHinting(tf, TTF_HINTING_LIGHT);
                TTF_Font* oldTitle = gFontTitle;
                gFontTitle = tf;
                int tw = 0, th = 0;
                TTF_SizeUTF8(gFontTitle, "M", &tw, &th);
                Console::setTitleFont(gFontTitle, tw > 0 ? tw : titlePx / 2,
                                      TTF_FontLineSkip(gFontTitle));
                if (oldTitle) TTF_CloseFont(oldTitle);
            }
        }
        if (oldBigBold && oldBigBold != oldBig) TTF_CloseFont(oldBigBold);
        if (oldBig) TTF_CloseFont(oldBig);
    }
    return true;
}

// Only runs when the window height actually changes. Reopening a TTF every
// frame is wasteful, and the size -> rows -> size loop would wobble by a pixel
// forever.
void refitFont(int outH) {
    if (outH == gLastFitH || gFontPath.empty()) return;
    gLastFitH = outH;
    int wantCell = std::max(8, (outH - 24) / TARGET_ROWS);
    int px = (gCellH > 0) ? (int)((float)wantCell * gFontPx / (float)gCellH + 0.5f)
                          : gFontPxIdeal;
    px = std::max(11, std::min(gFontPxIdeal, px));
    openFontsAt(px);
}

} // anonymous namespace

bool init(const char* title) {
    // Before SDL_Init, or Windows bitmap-stretches the window at 125%/150% and
    // blurs every glyph. Not SDL_WINDOWS_DPI_SCALING, that desyncs the mouse from
    // the renderer.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    // Says what went wrong, where the player can see it. Launched from a Dock
    // icon there is no terminal, so a line on stderr is a silent death.
    auto fail = [](const char* step, const char* err) {
        const std::string msg = std::string("Moonstruck could not start.\n\n") + step + " failed:\n"
                              + (err && *err ? err : "no reason given")
                              + "\n\nThere is a launch log at:\n" + Audio::saveDir() + "launch.log";
        std::cerr << msg << std::endl;
        Audio::logLaunch(std::string("FAILED at ") + step + ": " + (err ? err : ""));
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonstruck", msg.c_str(), nullptr);
        return false;
    };

    // Video only. Audio gets asked for separately below, because a Mac with a busy
    // or missing sound device failed this call and took the whole launch with it.
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return fail("SDL_Init (video)", SDL_GetError());
    Audio::logLaunch("sdl: video up");
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        Audio::logLaunch(std::string("sdl: no audio device, playing on in silence: ") + SDL_GetError());
        SDL_ClearError();
    } else {
        Audio::logLaunch("sdl: audio up");
    }
    if (TTF_Init() != 0) return fail("TTF_Init (text)", TTF_GetError());
    Audio::logLaunch("sdl: text engine up");
    // Controllers asked for on their own like audio, so without them you can still
    // play on keyboard and mouse. Pads already plugged in show up on frame one.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        Audio::logLaunch(std::string("sdl: no controller support, keyboard and mouse only: ") + SDL_GetError());
        SDL_ClearError();
    }

    // Starts maximised. Rows are the scarce thing in this layout (the battle screen
    // wants about 33), and on a scaled display a fixed 1600x900 doesn't have
    // enough once the font's readable.
    gWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DEFAULT_W, DEFAULT_H,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!gWindow) return fail("SDL_CreateWindow", SDL_GetError());
    Audio::logLaunch("sdl: window created");

    // The window icon (title bar, taskbar, and on Linux everything, since there's
    // no compiled-in resource there). A missing file isn't worth failing over.
    {
        int iw = 0, ih = 0, comp = 0;
        const std::string iconPath = Audio::dataDir() + "assets/icon.png";
        unsigned char* px = stbi_load(iconPath.c_str(), &iw, &ih, &comp, 4);
        if (px) {
            SDL_Surface* icon = SDL_CreateRGBSurfaceWithFormatFrom(
                px, iw, ih, 32, iw * 4, SDL_PIXELFORMAT_RGBA32);
            // SDL copies the pixels, so both can go straight back.
            if (icon) { SDL_SetWindowIcon(gWindow, icon); SDL_FreeSurface(icon); }
            stbi_image_free(px);
        }
    }

    gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!gRenderer) gRenderer = SDL_CreateRenderer(gWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!gRenderer) return fail("SDL_CreateRenderer", SDL_GetError());
    Audio::logLaunch("sdl: renderer created");
    // No SDL_RenderSetLogicalSize: see Platform.h. Drawing 1:1 against the real
    // output size is what keeps the text crisp.
    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);

    // DejaVu Sans Mono is bundled so the build is self contained and free to ship,
    // and it has the block glyphs the bars need. System fonts are only a fallback.
    const std::string base = Audio::dataDir();
    // Looked for in every folder a rearranged .app could leave it in.
    std::vector<std::string> tried;
    for (const std::string& dir : { base, Audio::exeDir(), Audio::exeDir() + "../Resources/" })
        tried.push_back(dir + "assets/DejaVuSansMono.ttf");
    // Then the system fonts per platform. Menlo lives under /System/Library on
    // macOS now: /Library/Fonts/Menlo.ttc is gone since Catalina, and a missing
    // font means a window with nothing in it.
    for (const char* p : {
            "/System/Library/Fonts/Menlo.ttc",
            "/System/Library/Fonts/SFNSMono.ttf",
            "/System/Library/Fonts/Monaco.ttf",
            "/System/Library/Fonts/Supplemental/Courier New.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
            "C:/Windows/Fonts/consola.ttf" })
        tried.push_back(p);

    // DPI-aware, so the font is scaled here: 19 logical px times the display's
    // scale factor, the same physical size at any scaling.
    float dpiScale = 1.0f;
    {
        float ddpi = 0, hdpi = 0, vdpi = 0;
        int di = SDL_GetWindowDisplayIndex(gWindow);
        float byDpi = (di >= 0 && SDL_GetDisplayDPI(di, &ddpi, &hdpi, &vdpi) == 0 && ddpi > 0)
                          ? ddpi / 96.0f : 1.0f;
        // Fallback for platforms where the backing store is larger than the
        // window (macOS Retina) rather than reported via DPI.
        int winW = 0, winH = 0, outW = 0, outH = 0;
        SDL_GetWindowSize(gWindow, &winW, &winH);
        SDL_GetRendererOutputSize(gRenderer, &outW, &outH);
        float byOut = (winW > 0) ? (float)outW / (float)winW : 1.0f;
        // max, not product - on any one platform only one of these reports the
        // scaling, and multiplying them would double-count it.
        dpiScale = std::max(1.0f, std::max(byDpi, byOut));
        if (dpiScale > 4.0f) dpiScale = 4.0f; // guard against bogus DPI reports
    }
    const int FONT_PX = (int)(19.0f * dpiScale + 0.5f);
    Audio::logLaunch("fonts: looking for a face");

    for (const std::string& p : tried) {
        TTF_Font* probe = TTF_OpenFont(p.c_str(), FONT_PX);
        if (probe) { TTF_CloseFont(probe); gFontPath = p; break; }
    }
    Audio::logLaunch(gFontPath.empty() ? "fonts: none opened" : "fonts: using " + gFontPath);
    if (gFontPath.empty()) {
        // Say so, rather than closing the window with nothing on screen.
        std::string msg = "Moonstruck could not open a monospace font.\n\nIt looked in:\n";
        for (const std::string& p : tried) msg += "  " + p + "\n";
        msg += "\nThe game ships its own font in assets/. If you moved the app's files "
               "around, put assets/ back beside the program (inside Contents/Resources on macOS).";
        std::cerr << msg << std::endl;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Moonstruck", msg.c_str(), nullptr);
        return false;
    }
    // The bold face the same way: next to whichever regular face was found,
    // then the regular one itself rather than nothing.
    gFontBoldPath.clear();
    for (const std::string& dir : { base, Audio::exeDir(), Audio::exeDir() + "../Resources/" }) {
        const std::string p = dir + "assets/DejaVuSansMono-Bold.ttf";
        TTF_Font* pb = TTF_OpenFont(p.c_str(), FONT_PX);
        if (pb) { TTF_CloseFont(pb); gFontBoldPath = p; break; }
    }
    if (gFontBoldPath.empty()) gFontBoldPath = gFontPath;

    gFontPxIdeal = FONT_PX;
    if (!openFontsAt(FONT_PX)) { std::cerr << "Could not open a monospace font" << std::endl; return false; }

    // Get the real viewport before anything prints. frame() refreshes it every
    // tick, but the title is written before the first frame, and centring against
    // the stale 1600x900 default put it off to the left.
    {
        int outW = DEFAULT_W, outH = DEFAULT_H;
        SDL_GetRendererOutputSize(gRenderer, &outW, &outH);
        Console::setViewport(24, 22, outW - 48, outH - 40);
    }
    Audio::logLaunch("console: ready, the window is up");
    Console::init(gFont, gFontBold, gCellW, gCellH);
    Console::installStdoutRedirect();

    gLastFrame = SDL_GetTicks();
    return true;
}

void shutdown() {
    Console::shutdown();
    if (gFontBold && gFontBold != gFont) TTF_CloseFont(gFontBold);
    if (gFont) TTF_CloseFont(gFont);
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    TTF_Quit();
    SDL_Quit();
}

SDL_Renderer* renderer() { return gRenderer; }
int cellW() { return gCellW; }
int cellH() { return gCellH; }

int screenW() { int w = DEFAULT_W, h; if (gRenderer) SDL_GetRendererOutputSize(gRenderer, &w, &h); return w; }
int screenH() { int w, h = DEFAULT_H; if (gRenderer) SDL_GetRendererOutputSize(gRenderer, &w, &h); return h; }

void frame() {
    pumpEvents();
    exitIfQuit();

    // Re-derive the text area every frame so resizing and fullscreen just work.
    int outW = DEFAULT_W, outH = DEFAULT_H;
    SDL_GetRendererOutputSize(gRenderer, &outW, &outH);
    refitFont(outH);
    stepShake();
    Console::setViewport(24 + gShakeX, 22 + gShakeY, outW - 48, outH - 40);

    SDL_SetRenderDrawColor(gRenderer, gGround.r, gGround.g, gGround.b, 255);
    SDL_RenderClear(gRenderer);

    if (gSceneRenderer) gSceneRenderer();
    // Panels draw BEFORE the text, they're backgrounds (the log panel sits right
    // behind the combat log). Drawing them after Console::render painted straight
    // over the log.
    if (gHudRenderer) gHudRenderer();
    Console::render(gRenderer);
    if (gOverlayRenderer) gOverlayRenderer();
    if (gHandRenderer) gHandRenderer();
    if (gTipRenderer) gTipRenderer();
    if (gModalRenderer) gModalRenderer();
    if (gToastRenderer) gToastRenderer();

    SDL_RenderPresent(gRenderer);

    // With vsync unavailable (software renderer), keep the loop from spinning.
    Uint32 now = SDL_GetTicks();
    if (now - gLastFrame < 8) SDL_Delay(8 - (now - gLastFrame));
    gLastFrame = SDL_GetTicks();
}

// Combat pacing: pause() and EnemyArt::hold() go through delay(), which scales
// by this. typeWrite() has its own clock. 150 is the authored rhythm, "Normal"
// in Settings. Higher is slower.
static int gPacePercent = 150;

void setPacePercent(int pct) { gPacePercent = pct < 10 ? 10 : (pct > 300 ? 300 : pct); }

void delay(int ms) {
    if (ms <= 0) { frame(); return; }
    ms = (int)(ms * (gPacePercent / 100.0f) + 0.5f);
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    do { frame(); } while ((Sint32)(end - SDL_GetTicks()) > 0);
}

KeyEvent pollKey() {
    if (gKeys.empty()) return KeyEvent{};
    KeyEvent k = gKeys.front();
    gKeys.pop_front();
    return k;
}

// A held direction keeps repeating on the next screen, as a held key does,
// but only after the full pause: the new screen does not start scrolling.
void flushKeys() {
    gKeys.clear(); gHasClick = false; gMouseDown = false;
    gTipSteps = 0;
    if (gHeldKey != Key::NONE) gHeldNext = SDL_GetTicks() + REPEAT_DELAY;
}

int takeTipSteps() { const int s = gTipSteps; gTipSteps = 0; return s; }
unsigned inputSerial() { return gSerial; }
bool pointerHidden() { return gPointerHidden; }

bool takeClick(int& x, int& y) {
    if (!gHasClick) return false;
    gHasClick = false;
    x = gClickX; y = gClickY;
    return true;
}

int takeWheel() {
    int w = gWheel;
    gWheel = 0;
    return w;
}

void mousePos(int& x, int& y) { x = gMouseX; y = gMouseY; }
bool mouseDown() { return gMouseDown; }

void setGroundColor(SDL_Color c) { gGround = c; }
void setSceneRenderer(const std::function<void()>& fn) { gSceneRenderer = fn; }
void setOverlayRenderer(const std::function<void()>& fn) { gOverlayRenderer = fn; }
void setHandRenderer(const std::function<void()>& fn) { gHandRenderer = fn; }
void setTipRenderer(const std::function<void()>& fn) { gTipRenderer = fn; }
void setHudRenderer(const std::function<void()>& fn) { gHudRenderer = fn; }
void setModalRenderer(const std::function<void()>& fn) { gModalRenderer = fn; }
void setToastRenderer(const std::function<void()>& fn) { gToastRenderer = fn; }

SDL_Color groundTone(int luma) {
    float cur = std::max(1.0f, (gGround.r * 77 + gGround.g * 150 + gGround.b * 29) / 256.0f);
    float k = (float)luma / cur;
    auto ch = [&](Uint8 v) { return (Uint8)std::min(255, (int)(v * k + 0.5f)); };
    return SDL_Color{ ch(gGround.r), ch(gGround.g), ch(gGround.b), 255 };
}

void shake(int ms, float strength) {
    // Re-triggering mid-shake restarts rather than stacking: two hits in quick
    // succession should read as two knocks, not one long rattle.
    gShakeStart = SDL_GetTicks();
    gShakeMs    = (Uint32)std::max(1, ms);
    gShakeMag   = strength;
}

void shakeOffset(int& dx, int& dy) { dx = gShakeX; dy = gShakeY; }


} // namespace Platform
