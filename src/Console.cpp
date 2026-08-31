#include "Console.h"

#include <algorithm>
#include <iostream>
#include <streambuf>
#include <unordered_map>

namespace Console {
namespace {

// --- grid state ---
std::vector<std::vector<Cell>> gLines;
int gCursorRow = 0, gCursorCol = 0;
int gSavedRow = 0, gSavedCol = 0; // DECSC / DECRC
int gSceneRows = 0;

SDL_Color gCurFg{ 205, 205, 215, 255 };
bool gCurBold = false;

TTF_Font* gFont = nullptr;
TTF_Font* gFontBold = nullptr;
int gCellW = 9, gCellH = 20;
SDL_Rect gViewport{ 0, 0, 1600, 900 };

// --- ANSI parser state, persistent across write() calls so an escape or a
// multi-byte UTF-8 sequence split across two writes still parses correctly ---
enum class PState { TEXT, ESC, CSI };
PState gState = PState::TEXT;
std::string gCsi;
unsigned gUtf8Need = 0;
char32_t gUtf8Acc = 0;

// 256-color palette lookup, so 38;5;N (ORANGE, the rarity tints, SELECT_CLR's
// neon lime) resolves to the same color the terminal showed.
//
// Indices 0-15 are the Campbell scheme - the default in Windows Terminal,
// which is what hosts console apps on Windows 10/11, so these are the actual
// RGB values the terminal build displayed. Using xterm's pure primaries here
// instead made energy a neon (255,255,0) and special cards a neon (255,0,255),
// both far harsher than the real thing. Indices 16-255 are the standard xterm
// cube, which Windows Terminal uses unchanged.
SDL_Color xterm256(int n) {
    static const unsigned char basic[16][3] = {
        { 12, 12, 12}, {197, 15, 31}, { 19,161, 14}, {193,156,  0},
        {  0, 55,218}, {136, 23,152}, { 58,150,221}, {204,204,204},
        {118,118,118}, {231, 72, 86}, { 22,198, 12}, {249,241,165},
        { 59,120,255}, {180,  0,158}, { 97,214,214}, {242,242,242},
    };
    if (n < 0) n = 0;
    if (n < 16) return SDL_Color{ basic[n][0], basic[n][1], basic[n][2], 255 };
    if (n < 232) {
        static const int lv[6] = { 0, 95, 135, 175, 215, 255 };
        int i = n - 16;
        return SDL_Color{ (Uint8)lv[(i / 36) % 6], (Uint8)lv[(i / 6) % 6], (Uint8)lv[i % 6], 255 };
    }
    Uint8 g = (Uint8)(8 + (n - 232) * 10);
    return SDL_Color{ g, g, g, 255 };
}

// The 8 standard + 8 bright ANSI colors. These map onto the same palette
// entries a terminal uses, rather than a hand-muted set - an earlier version
// dimmed them "for legibility" and made the HP bars read as too dark compared
// to the terminal build.
SDL_Color ansiBasic(int code) {
    if (code >= 30 && code <= 37) return xterm256(code - 30);
    if (code >= 90 && code <= 97) return xterm256(code - 90 + 8);
    return SDL_Color{ 205, 205, 215, 255 };
}

void ensureRow(int row) {
    while ((int)gLines.size() <= row) gLines.push_back({});
}

void putChar(char32_t ch) {
    if (gCursorRow < 0) gCursorRow = 0;
    ensureRow(gCursorRow);
    auto& line = gLines[gCursorRow];
    while ((int)line.size() <= gCursorCol) line.push_back(Cell{});
    line[gCursorCol] = Cell{ ch, gCurFg, gCurBold };
    gCursorCol++;
}

void applySgr(const std::string& body) {
    // Split on ';' and walk the parameters; 38;5;N consumes two extras.
    std::vector<int> p;
    size_t i = 0;
    while (i <= body.size()) {
        size_t j = body.find(';', i);
        std::string tok = body.substr(i, j == std::string::npos ? std::string::npos : j - i);
        p.push_back(tok.empty() ? 0 : atoi(tok.c_str()));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    for (size_t k = 0; k < p.size(); k++) {
        int c = p[k];
        if (c == 0)      { gCurFg = SDL_Color{205,205,215,255}; gCurBold = false; }
        else if (c == 1) { gCurBold = true; }
        else if (c == 2) { gCurFg = SDL_Color{130,130,145,255}; }
        else if (c == 22){ gCurBold = false; }
        else if (c == 39){ gCurFg = SDL_Color{205,205,215,255}; }
        else if ((c >= 30 && c <= 37) || (c >= 90 && c <= 97)) gCurFg = ansiBasic(c);
        else if (c == 38 && k + 2 < p.size() && p[k + 1] == 5) { gCurFg = xterm256(p[k + 2]); k += 2; }
    }
}

void handleCsi(const std::string& seq) {
    if (seq.empty()) return;
    char final = seq.back();
    std::string body = seq.substr(0, seq.size() - 1);
    int n = body.empty() ? 0 : atoi(body.c_str());
    switch (final) {
        case 'm': applySgr(body); break;
        case 'A': gCursorRow = std::max(0, gCursorRow - std::max(1, n)); break;
        case 'B': gCursorRow += std::max(1, n); ensureRow(gCursorRow); break;
        case 'C': gCursorCol += std::max(1, n); break;
        case 'D': gCursorCol = std::max(0, gCursorCol - std::max(1, n)); break;
        case 'G': gCursorCol = std::max(0, n - 1); break;
        case 'H': case 'f': gCursorRow = std::max(0, n - 1); gCursorCol = 0; break;
        case 'J': clear(); break;
        case 'K': // erase in line
            if (gCursorRow < (int)gLines.size()) {
                auto& line = gLines[gCursorRow];
                if (n == 0 && gCursorCol < (int)line.size()) line.resize(gCursorCol);
                else if (n == 2) line.clear();
            }
            break;
        default: break;
    }
}

// Feeds one byte through the escape/UTF-8 state machine.
void feed(unsigned char b) {
    if (gState == PState::ESC) {
        if (b == '[')      { gState = PState::CSI; gCsi.clear(); }
        else if (b == '7') { gSavedRow = gCursorRow; gSavedCol = gCursorCol; gState = PState::TEXT; }
        else if (b == '8') { gCursorRow = gSavedRow; gCursorCol = gSavedCol; gState = PState::TEXT; }
        else               { gState = PState::TEXT; }
        return;
    }
    if (gState == PState::CSI) {
        gCsi.push_back((char)b);
        if (b >= 0x40 && b <= 0x7E) { handleCsi(gCsi); gState = PState::TEXT; }
        return;
    }

    if (gUtf8Need > 0) { // continuation byte of a multi-byte codepoint
        if ((b & 0xC0) == 0x80) {
            gUtf8Acc = (gUtf8Acc << 6) | (b & 0x3F);
            if (--gUtf8Need == 0) putChar(gUtf8Acc);
            return;
        }
        gUtf8Need = 0; // malformed - fall through and treat b as a fresh byte
    }

    if (b == 0x1B) { gState = PState::ESC; return; }
    if (b == '\n') { gCursorRow++; gCursorCol = 0; ensureRow(gCursorRow); return; }
    if (b == '\r') { gCursorCol = 0; return; }
    if (b == '\t') { gCursorCol = (gCursorCol / 8 + 1) * 8; return; }
    if (b == '\b') { gCursorCol = std::max(0, gCursorCol - 1); return; }
    if (b < 0x20)  return;

    if (b < 0x80)             { putChar(b); return; }
    if ((b & 0xE0) == 0xC0)   { gUtf8Acc = b & 0x1F; gUtf8Need = 1; return; }
    if ((b & 0xF0) == 0xE0)   { gUtf8Acc = b & 0x0F; gUtf8Need = 2; return; }
    if ((b & 0xF8) == 0xF0)   { gUtf8Acc = b & 0x07; gUtf8Need = 3; return; }
}

// --- glyph cache: one texture per (codepoint, color, bold) ---
struct GlyphKey {
    char32_t ch; Uint32 rgba; bool bold;
    bool operator==(const GlyphKey& o) const { return ch == o.ch && rgba == o.rgba && bold == o.bold; }
};
struct GlyphHash {
    size_t operator()(const GlyphKey& k) const {
        return std::hash<Uint64>()(((Uint64)k.ch << 33) ^ ((Uint64)k.rgba << 1) ^ (Uint64)k.bold);
    }
};
std::unordered_map<GlyphKey, SDL_Texture*, GlyphHash> gGlyphs;

// Encodes a codepoint back to UTF-8 for TTF_RenderUTF8_Blended.
std::string toUtf8(char32_t c) {
    std::string s;
    if (c < 0x80) s += (char)c;
    else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
        s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F));
    } else {
        s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F));
        s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F));
    }
    return s;
}

SDL_Texture* glyphTexture(SDL_Renderer* r, const Cell& cell) {
    Uint32 rgba = ((Uint32)cell.fg.r << 24) | ((Uint32)cell.fg.g << 16) | ((Uint32)cell.fg.b << 8) | 255u;
    GlyphKey key{ cell.ch, rgba, cell.bold };
    auto it = gGlyphs.find(key);
    if (it != gGlyphs.end()) return it->second;

    TTF_Font* font = (cell.bold && gFontBold) ? gFontBold : gFont;
    if (!font) return nullptr;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, toUtf8(cell.ch).c_str(), cell.fg);
    SDL_Texture* tex = surf ? SDL_CreateTextureFromSurface(r, surf) : nullptr;
    if (surf) SDL_FreeSurface(surf);
    gGlyphs[key] = tex;
    return tex;
}

// --- std::cout redirect ---
class ConsoleBuf : public std::streambuf {
protected:
    int overflow(int c) override {
        if (c != EOF) { char ch = (char)c; Console::write(&ch, 1); }
        return c;
    }
    std::streamsize xsputn(const char* s, std::streamsize n) override {
        Console::write(s, (size_t)n);
        return n;
    }
};
ConsoleBuf* gBuf = nullptr;

// Raw bytes, escapes and all, so replaying a line reproduces its colour.
bool gCapture = false;
std::string gCapLine;
std::vector<std::string> gHistory;
// A full run is ~44 encounters; at roughly 40 logged lines each this holds the
// whole thing with room to spare, and it is only strings.
constexpr size_t HISTORY_MAX = 4000;

void captureByte(unsigned char ch) {
    if (!gCapture) return;
    if (ch == 10) {                       // newline ends a line
        // Skip blank-after-strip lines: the log is dense enough without them.
        bool any = false;
        for (size_t i = 0; i < gCapLine.size(); i++)
            if ((unsigned char)gCapLine[i] > 32) { any = true; break; }
        if (any) {
            gHistory.push_back(gCapLine);
            if (gHistory.size() > HISTORY_MAX)
                gHistory.erase(gHistory.begin(), gHistory.begin() + (gHistory.size() - HISTORY_MAX));
        }
        gCapLine.clear();
    } else if (ch != 13) {
        gCapLine.push_back((char)ch);
    }
}

} // anonymous namespace

void write(const char* bytes, size_t n) {
    for (size_t i = 0; i < n; i++) { captureByte((unsigned char)bytes[i]); feed((unsigned char)bytes[i]); }
}
void write(const std::string& s) { write(s.data(), s.size()); }

void clear() {
    gLines.clear();
    gCursorRow = gCursorCol = 0;
    gSavedRow = gSavedCol = 0;
    // Clearing the screen also drops the battle scene, so menus and reward
    // screens get the full window. Battle screens call clearScreen() and then
    // EnemyArt::printBattle() back-to-back with no frame drawn in between, so
    // the scene never visibly flickers off during combat.
    gSceneRows = 0;
}

int cursorRow() { return gCursorRow; }
void setCursorRow(int row) { gCursorRow = std::max(0, row); gCursorCol = 0; ensureRow(gCursorRow); }

int rows() { return std::max(1, gViewport.h / gCellH) - gSceneRows; }
int cols() { return std::max(1, gViewport.w / gCellW); }

void setSceneRows(int r) { gSceneRows = std::max(0, r); }
int  sceneRows() { return gSceneRows; }

void init(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH) {
    gFont = regular; gFontBold = bold; gCellW = cellW; gCellH = cellH;
}
void setHistoryCapture(bool on) {
    if (!on && !gCapLine.empty()) { captureByte(10); }   // flush a partial line
    gCapture = on;
}
const std::vector<std::string>& history() { return gHistory; }

// Append a line regardless of capture state - used for the encounter markers
// that divide the log, which are written while capture is off.
void pushHistory(const std::string& line) {
    gHistory.push_back(line);
    if (gHistory.size() > HISTORY_MAX)
        gHistory.erase(gHistory.begin(), gHistory.begin() + (gHistory.size() - HISTORY_MAX));
}
void clearHistory() { gHistory.clear(); gCapLine.clear(); }

void setFont(TTF_Font* regular, TTF_Font* bold, int cellW, int cellH) {
    for (auto& kv : gGlyphs) if (kv.second) SDL_DestroyTexture(kv.second);
    gGlyphs.clear();
    gFont = regular; gFontBold = bold; gCellW = cellW; gCellH = cellH;
}


void setViewport(int x, int y, int w, int h) { gViewport = SDL_Rect{ x, y, w, h }; }

namespace {
// Shared by render() and rowAtY() so hit-testing always uses the exact scroll
// offset the last frame was drawn with.
int scrollStart() {
    int visibleRows = std::max(1, gViewport.h / gCellH - gSceneRows);
    int lastRow = std::max((int)gLines.size(), gCursorRow + 1);
    return std::max(0, lastRow - visibleRows);
}
} // namespace

int rowAtY(int y) {
    int originY = gViewport.y + gSceneRows * gCellH;
    if (y < originY) return -1;
    int row = scrollStart() + (y - originY) / gCellH;
    return row;
}

void render(SDL_Renderer* renderer) {
    if (!gFont) return;
    // Keep the cursor in view: scroll so the most recently written row is the
    // last one drawn, which matches how the terminal scrolled.
    int start = scrollStart();
    int originY = gViewport.y + gSceneRows * gCellH;

    for (int i = start; i < (int)gLines.size(); i++) {
        const auto& line = gLines[i];
        int y = originY + (i - start) * gCellH;
        if (y + gCellH > gViewport.y + gViewport.h) break;
        for (int c = 0; c < (int)line.size(); c++) {
            const Cell& cell = line[c];
            if (cell.ch == U' ') continue;
            SDL_Texture* tex = glyphTexture(renderer, cell);
            if (!tex) continue;
            int tw = 0, th = 0;
            SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
            SDL_Rect dst{ gViewport.x + c * gCellW, y, tw, th };
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
        }
    }
}

void shutdown() {
    for (auto& kv : gGlyphs) if (kv.second) SDL_DestroyTexture(kv.second);
    gGlyphs.clear();
}

void installStdoutRedirect() {
    if (gBuf) return;
    gBuf = new ConsoleBuf();
    std::cout.rdbuf(gBuf);
    std::cout.setf(std::ios::unitbuf); // draw as the game writes, not at flush points
}

int visibleLen(const std::string& s) {
    int len = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = (unsigned char)s[i];
        if (b == 0x1B) { // skip the whole escape sequence
            i++;
            if (i < s.size() && s[i] == '[') {
                i++;
                while (i < s.size() && !(s[i] >= 0x40 && s[i] <= 0x7E)) i++;
            }
            i++;
            continue;
        }
        if ((b & 0xC0) != 0x80) len++; // count lead bytes only
        i++;
    }
    return len;
}

} // namespace Console
