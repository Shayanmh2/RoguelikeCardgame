#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

// A virtual terminal: std::cout is redirected into this grid, ANSI escapes
// become per-cell colours, and the grid is drawn with SDL_ttf.
namespace Console {

struct Cell {
    char32_t ch = U' ';
    SDL_Color fg{ 205, 205, 215, 255 };
    bool bold = false;
};

// --- writing (the std::cout redirect calls this) ---
void write(const char* bytes, size_t n);
void write(const std::string& s);

// --- screen control, mirroring the escapes UIHelper/Game.cpp emit ---
void clear();

// Rows/cols left for text after the reserved bands take their share.
int rows();
int cols();

// Height in rows of the region reserved above the text for the battle scene;
// EnemyArt sets this so text flows underneath the sprites instead of behind.
void setSceneRows(int r);
int  sceneRows();

// The bottom counterpart of setSceneRows: rows withheld from the text so the
// card hand can be drawn there as real widgets. Reset by clear().
void setHandRows(int r);
int  handRows();
int  totalRows();
SDL_Rect handRegion();

// Band between the scene and the text, where the combat panel is drawn.
void setHudRows(int r);
SDL_Rect hudRegion();
int sceneOriginY();
SDL_Rect textRegion();

// Text at a pixel position, for widgets drawn in the reserved bands.
void drawTextPx(SDL_Renderer* r, int x, int y, const std::string& text,
                SDL_Color color, bool bold);

// Draws text that already carries ANSI colour, keeping it. Returns the x the
// run ended at, so callers can lay the next thing out after it.
int drawAnsiPx(SDL_Renderer* r, int x, int y, const std::string& text,
               SDL_Color base, bool bold);

// Widget-sized face. Its own point size, not a scale-up; see Console.cpp.
void setBigFont(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH);
void drawTextBigPx(SDL_Renderer* r, int x, int y, const std::string& text,
                   SDL_Color color, bool bold);
int  bigCellW();

int  bigCellH();

// Title-sized face, larger again.
void setDisplayFont(TTF_Font* f, int cellW, int cellH);
// The wordmark's own face: bigger than the headline face, and optionally a
// different typeface entirely (assets/title.ttf).
void setTitleFont(TTF_Font* f, int cellW, int cellH);
void drawTextDispPx(SDL_Renderer* r, int x, int y, const std::string& text, SDL_Color color);
// `tracking` adds pixels between letters: a wordmark wants air that running
// text does not.
void drawTextTitlePx(SDL_Renderer* r, int x, int y, const std::string& text, SDL_Color color,
                     int tracking = 0);
int  dispCellW();
int  titleCellW();

// The palette the ANSI codes resolve to: Campbell for 0-15, the xterm cube
// above. Widgets use it so a card name matches the colour the log prints.
SDL_Color xterm256Public(int n);

// --- rendering ---
void init(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH);

// Swap to a different point size. Clears the glyph cache, whose keys carry
// no size - stale textures would otherwise be drawn at the new cell size.
void setFont(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH);

// Action history, kept separately because clear() wipes the screen buffer.
// Capture is opt-in: the battle screen redraws in full every turn, and
// logging that would bury the events under repeated headers and card lists.
void setHistoryCapture(bool on);
const std::vector<std::string>& history();
void pushHistory(const std::string& line);
void clearHistory();
void setViewport(int x, int y, int w, int h);
void render(SDL_Renderer* renderer);
void shutdown();

// Redirects std::cout into this console for the process's lifetime.
void installStdoutRedirect();

// Visible length of a string, ignoring ANSI escapes (UIHelper::visibleLen).
int visibleLen(const std::string& s);

} // namespace Console
