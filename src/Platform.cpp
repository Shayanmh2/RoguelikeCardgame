#include "Platform.h"
#include "Console.h"
#include "Audio.h"

#include "stb_image.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <string>

namespace Platform {
namespace {

SDL_Window*   gWindow = nullptr;
SDL_Renderer* gRenderer = nullptr;
TTF_Font*     gFont = nullptr;
TTF_Font*     gFontBold = nullptr;
int gCellW = 9, gCellH = 20;

std::deque<KeyEvent> gKeys;
bool gHasClick = false;
int  gWheel = 0;              // accumulated wheel notches, consumed by takeWheel
int  gClickX = 0, gClickY = 0;
int  gMouseX = 0, gMouseY = 0;
bool gQuit = false;

std::function<void()> gSceneRenderer;
std::function<void()> gOverlayRenderer;
std::function<void()> gHandRenderer;
std::function<void()> gHudRenderer;
std::function<void()> gModalRenderer;
SDL_Color gGround{ 13, 13, 15, 255 };

// Font sizing state. The point size is not fixed: a short window has too few
// rows for both the combat text and a scene worth looking at, so the font is
// refitted whenever the window height changes.
std::string gFontPath, gFontBoldPath;
int gFontPxIdeal = 19;      // the DPI-correct size; never exceeded
int gFontPx = 0;            // what is currently loaded
int gLastFitH = -1;         // output height the current fit was computed for
TTF_Font* gFontBig = nullptr;
TTF_Font* gFontBigBold = nullptr;
TTF_Font* gFontDisp = nullptr;

Uint32 gShakeStart = 0, gShakeMs = 0;
float  gShakeMag = 0.0f;
int    gShakeX = 0, gShakeY = 0;

// Recomputed once per frame so every consumer in that frame agrees on the
// offset. Amplitude falls off quadratically, which reads as an impact settling
// rather than a vibration stopping dead.
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

void pumpEvents() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT:
                gQuit = true;
                break;
            case SDL_KEYDOWN: {
                KeyEvent k;
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
                    gClickX = ev.button.x;
                    gClickY = ev.button.y;
                }
                break;
            case SDL_MOUSEMOTION:
                gMouseX = ev.motion.x;
                gMouseY = ev.motion.y;
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
}

// Closing the window has to work even though the game is several blocking
// calls deep inside Game::run(). Unwinding that by hand would mean threading a
// quit check through every loop in Game.cpp; instead shut down cleanly here.
void exitIfQuit() {
    if (!gQuit) return;
    Audio::stopBGM();
    Console::shutdown();
    Audio::shutdown();
    if (gFont) TTF_CloseFont(gFont);
    if (gFontBold) TTF_CloseFont(gFontBold);
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    TTF_Quit();
    SDL_Quit();
    std::exit(0);
}

// Rows the battle screen wants: EnemyArt reserves MIN_TEXT_ROWS (27) for the
// header, status block and card menu, and the scene lives on what is left. Ask
// for enough that the scene gets a dozen rows rather than its 3x floor.
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

        // Title face, shared by the title screen and the headline overlay.
        // Sized off the grid font, then capped so the longest thing it draws
        // still fits the window width. The grid font tracks window HEIGHT, so
        // without the cap a tall narrow window picks a title too wide for it,
        // and the callers drop to the widget face - a jarring cliff rather
        // than a title one step smaller. 18 characters is "ROGUELIKE
        // CARDGAME"; the advance of this face is about 0.6 of its point size.
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
        if (oldBigBold && oldBigBold != oldBig) TTF_CloseFont(oldBigBold);
        if (oldBig) TTF_CloseFont(oldBig);
    }
    return true;
}

// Only runs when the window height actually changes - reopening a TTF every
// frame would be wasteful, and the feedback loop (size -> rows -> size) would
// oscillate by a pixel forever.
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
    // Before SDL_Init, or the process is DPI-unaware and Windows bitmap-
    // stretches the window on any display at 125%/150% - blurring every glyph
    // however crisply we drew it. Per-monitor v2 gets real physical pixels.
    //
    // Not SDL_WINDOWS_DPI_SCALING though: that switches SDL to DPI-scaled
    // points and desyncs mouse events from renderer output size.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
        return false;
    }
    if (TTF_Init() != 0) {
        std::cerr << "TTF_Init: " << TTF_GetError() << "\n";
        return false;
    }

    // Starts maximized, as the terminal build did (it called ShowWindow with
    // SW_MAXIMIZE). Rows are the scarce resource for this layout - the battle
    // screen wants ~33 of them - and on a scaled display a fixed 1600x900 just
    // doesn't have enough once the font is scaled up to a readable size.
    gWindow = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               DEFAULT_W, DEFAULT_H,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!gWindow) { std::cerr << "CreateWindow: " << SDL_GetError() << "\n"; return false; }

    // The window's own icon: the title bar, the running taskbar button, and on
    // Linux the whole story, since there is no compiled-in resource there.
    // A missing file is not worth failing startup over.
    {
        int iw = 0, ih = 0, comp = 0;
        const std::string iconPath = Audio::exeDir() + "assets/icon.png";
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
    if (!gRenderer) { std::cerr << "CreateRenderer: " << SDL_GetError() << "\n"; return false; }
    // No SDL_RenderSetLogicalSize: see Platform.h. Drawing 1:1 against the real
    // output size is what keeps the text crisp.
    SDL_SetRenderDrawBlendMode(gRenderer, SDL_BLENDMODE_BLEND);

    // DejaVu Sans Mono, bundled next to the exe so the build is self-contained
    // and legally redistributable on every platform (its license permits that;
    // Consolas, which this used to ship, does not). It also has the tightest
    // line spacing of the candidates that carry the block-drawing glyphs the
    // HP and armor bars need - worth ~6 extra rows, which this layout spends
    // on a larger battle scene. The system paths below are only a fallback for
    // a build whose assets folder went missing.
    const std::string base = Audio::exeDir();
    const char* regularCandidates[] = {
        nullptr, "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/Library/Fonts/Menlo.ttc", "C:/Windows/Fonts/consola.ttf",
    };
    std::string bundled = base + "assets/DejaVuSansMono.ttf";
    regularCandidates[0] = bundled.c_str();

    // Now that the process is DPI-aware, the window is handed real physical
    // pixels instead of a stretched virtual surface - so a fixed point size
    // renders physically smaller the higher the display's scaling is set. A
    // DPI-aware app has to do the scaling itself, which is what this does:
    // 19 logical px multiplied up by the display's scale factor, so the text
    // is the same physical size at 100%, 125% or 150% - just sharp now.
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

    for (const char* p : regularCandidates) {
        if (!p) continue;
        TTF_Font* probe = TTF_OpenFont(p, FONT_PX);
        if (probe) { TTF_CloseFont(probe); gFontPath = p; break; }
    }
    if (gFontPath.empty()) { std::cerr << "Could not open a monospace font" << std::endl; return false; }
    gFontBoldPath = base + "assets/DejaVuSansMono-Bold.ttf";
    { TTF_Font* pb = TTF_OpenFont(gFontBoldPath.c_str(), FONT_PX);
      if (pb) TTF_CloseFont(pb); else gFontBoldPath = gFontPath; }

    gFontPxIdeal = FONT_PX;
    if (!openFontsAt(FONT_PX)) { std::cerr << "Could not open a monospace font" << std::endl; return false; }

    // Establish the real viewport before anything prints. frame() refreshes it
    // every tick, but the title screen is written before the first frame, and
    // centring against the stale 1600x900 default put it left of centre.
    {
        int outW = DEFAULT_W, outH = DEFAULT_H;
        SDL_GetRendererOutputSize(gRenderer, &outW, &outH);
        Console::setViewport(24, 22, outW - 48, outH - 40);
    }
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
    // Panels draw BEFORE the text: they are backgrounds, and the log panel sits
    // directly behind the combat log. Running this after Console::render filled
    // straight over those lines.
    if (gHudRenderer) gHudRenderer();
    Console::render(gRenderer);
    if (gOverlayRenderer) gOverlayRenderer();
    if (gHandRenderer) gHandRenderer();
    if (gModalRenderer) gModalRenderer();

    SDL_RenderPresent(gRenderer);

    // With vsync unavailable (software renderer), keep the loop from spinning.
    Uint32 now = SDL_GetTicks();
    if (now - gLastFrame < 8) SDL_Delay(8 - (now - gLastFrame));
    gLastFrame = SDL_GetTicks();
}

// Combat pacing. The terminal build got its rhythm partly for free: a battle
// redraw cost tens of milliseconds through the Windows console. Those writes
// are nearly free here, so the same pause() values play back quicker.
//
// pause() and EnemyArt::hold() both route through delay(), so scaling here
// keeps every authored duration in proportion. typeWrite() runs off its own
// clock and is left alone - typing speed should not move when this is retuned.
//
// Raise to slow combat down.
static const float PACE_SCALE = 1.5f;

void delay(int ms) {
    if (ms <= 0) { frame(); return; }
    ms = (int)(ms * PACE_SCALE + 0.5f);
    Uint32 end = SDL_GetTicks() + (Uint32)ms;
    do { frame(); } while ((Sint32)(end - SDL_GetTicks()) > 0);
}

KeyEvent pollKey() {
    if (gKeys.empty()) return KeyEvent{};
    KeyEvent k = gKeys.front();
    gKeys.pop_front();
    return k;
}

KeyEvent waitKey() {
    while (true) {
        if (!gKeys.empty()) {
            KeyEvent k = gKeys.front();
            gKeys.pop_front();
            return k;
        }
        frame();
    }
}

void flushKeys() { gKeys.clear(); gHasClick = false; }

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

void setGroundColor(SDL_Color c) { gGround = c; }
void setSceneRenderer(const std::function<void()>& fn) { gSceneRenderer = fn; }
void setOverlayRenderer(const std::function<void()>& fn) { gOverlayRenderer = fn; }
void setHandRenderer(const std::function<void()>& fn) { gHandRenderer = fn; }
void setHudRenderer(const std::function<void()>& fn) { gHudRenderer = fn; }
void setModalRenderer(const std::function<void()>& fn) { gModalRenderer = fn; }

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
