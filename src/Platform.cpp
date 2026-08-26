#include "Platform.h"
#include "Console.h"
#include "Audio.h"

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
int  gClickX = 0, gClickY = 0;
int  gMouseX = 0, gMouseY = 0;
bool gQuit = false;

std::function<void()> gSceneRenderer;

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

} // anonymous namespace

bool init(const char* title) {
    // Must be set before SDL_Init. Without it the process is DPI-unaware, so
    // on any display running at 125%/150% scaling Windows renders the window
    // at a smaller virtual size and bitmap-stretches the result - which blurs
    // every glyph no matter how crisply we draw it. Declaring per-monitor v2
    // awareness gets us real physical pixels instead.
    //
    // Deliberately NOT setting SDL_WINDOWS_DPI_SCALING: that one switches SDL's
    // coordinate system to DPI-scaled points, which would desync mouse events
    // from renderer output size. Left off, everything stays in pixels.
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
        gFont = TTF_OpenFont(p, FONT_PX);
        if (gFont) break;
    }
    if (!gFont) { std::cerr << "Could not open a monospace font\n"; return false; }
    TTF_SetFontHinting(gFont, TTF_HINTING_LIGHT);

    std::string bundledBold = base + "assets/DejaVuSansMono-Bold.ttf";
    gFontBold = TTF_OpenFont(bundledBold.c_str(), FONT_PX);
    if (!gFontBold) gFontBold = gFont;
    else TTF_SetFontHinting(gFontBold, TTF_HINTING_LIGHT);

    // Monospace: every glyph advances the same, so one measurement sets the grid.
    TTF_SizeUTF8(gFont, "M", &gCellW, &gCellH);
    if (gCellW <= 0) gCellW = 9;
    gCellH = TTF_FontLineSkip(gFont);
    if (gCellH <= 0) gCellH = 20;

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
    Console::setViewport(24, 12, outW - 48, outH - 24);

    SDL_SetRenderDrawColor(gRenderer, 13, 13, 15, 255); // matches the terminal theme
    SDL_RenderClear(gRenderer);

    if (gSceneRenderer) gSceneRenderer();
    Console::render(gRenderer);

    SDL_RenderPresent(gRenderer);

    // With vsync unavailable (software renderer), keep the loop from spinning.
    Uint32 now = SDL_GetTicks();
    if (now - gLastFrame < 8) SDL_Delay(8 - (now - gLastFrame));
    gLastFrame = SDL_GetTicks();
}

// Combat pacing compensation.
//
// The terminal build got a lot of its rhythm for free: every std::cout of a
// battle redraw went through the Windows console, which costs tens of
// milliseconds a screenful. Here the same writes land in a memory grid and cost
// essentially nothing, so a turn built out of identical pause() values plays
// back noticeably quicker - the pauses are honest, but the work between them
// vanished.
//
// Scaling here rather than at the call sites keeps every authored duration in
// one place: UIHelper::pause() and EnemyArt::hold() both route through delay(),
// so animation holds and read-the-text beats stay in the proportion they were
// written in. typeWrite() and the menu idle tick run off their own clocks and
// are deliberately untouched - per-character typing speed should not drift when
// this is retuned.
//
// Raise to slow combat down, lower to speed it up. 1.0 is the terminal build's
// nominal timing, which in practice plays faster than the terminal did.
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

void mousePos(int& x, int& y) { x = gMouseX; y = gMouseY; }

void setSceneRenderer(const std::function<void()>& fn) { gSceneRenderer = fn; }

bool quitRequested() { return gQuit; }

} // namespace Platform
