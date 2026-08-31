#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <vector>

// A virtual terminal.
//
// Game.cpp is unchanged from the terminal build: it still writes colored text
// with std::cout and ANSI escapes. Rather than rewrite ~3400 lines of game
// logic into a retained-mode UI (which is how the earlier hand-transcribed
// Battle.cpp drifted out of sync with the original), std::cout is redirected
// into this grid, the escapes are parsed into per-cell colors, and the grid is
// drawn with SDL_ttf. The game's own text layout and color choices survive
// exactly as authored.
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
int  cursorRow();
void setCursorRow(int row);

// Number of text rows/cols currently available in the console region.
int rows();
int cols();

// Height in rows of the region reserved above the text for the battle scene;
// EnemyArt sets this so text flows underneath the sprites instead of behind.
void setSceneRows(int r);
int  sceneRows();

// --- rendering ---
void init(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH);

// Swap to a different point size. Clears the glyph cache, whose keys carry
// no size - stale textures would otherwise be drawn at the new cell size.
void setFont(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH);

// Action history. gLines only holds the current screen - it is wiped by every
// clear() - so a persistent log needs its own store. Capture is opt-in because
// the battle screen is fully redrawn each turn, and logging that redraw would
// bury the actual events under repeated headers and card lists.
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

// Maps a logical-pixel y back to the buffer row drawn there, accounting for
// the current scroll offset. Menus use this to hit-test clicks against the
// rows they printed. Returns -1 outside the text area.
int rowAtY(int y);

} // namespace Console
