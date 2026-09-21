// SDL2 build of UIHelper. Everything that only writes text is unchanged from
// the terminal version - std::cout is redirected into the Console grid, which
// understands the same ANSI escapes, so all the layout and color code below
// renders exactly as it did in the terminal. Only the four genuinely
// platform-bound pieces are reimplemented: sleeping, reading a key, clearing
// the screen, and the cursor row queries.
#include "UIHelper.h"
#include "Colors.h"
#include "Console.h"
#include "Platform.h"
#include "EnemyArt.h"
#include <iostream>
#include <cmath>
#include <cctype>
#include <algorithm>

// Sleeping now pumps the SDL event/render loop instead of blocking the thread,
// so the window keeps drawing (and stays closable) during every game pause.
static void platSleep(int ms) { Platform::delay(ms); }
static void flushInputBuffer() { Platform::flushKeys(); }

void UIHelper::printLine(int width, char c) {
    std::cout << Color::DIM;
    for (int i = 0; i < width; ++i) std::cout << c;
    std::cout << Color::RESET << "\n";
}

void UIHelper::printCentered(const std::string& text, int width) {
    // Centre on the window, not on a fixed 60 columns. The old measure put
    // everything left of centre on any window wider than that, which is every
    // window this build actually runs in.
    int cols = std::max(width, Console::cols());
    int padding = (cols - visibleLen(text)) / 2;
    if (padding < 0) padding = 0;
    std::cout << std::string((size_t)padding, ' ') << text << "\n";
}

// Wraps to a readable measure and centres every line. Story text is written as
// long paragraphs; at full window width it becomes an unreadable ribbon.
void UIHelper::printCenteredWrapped(const std::string& text, int measure, bool typed) {
    int cols = std::max(20, std::min(measure, Console::cols() - 4));
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i <= text.size()) {
        size_t sp = text.find(' ', i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        if (cur.empty()) cur = word;
        else if (visibleLen(cur) + 1 + visibleLen(word) <= cols) cur += " " + word;
        else { out.push_back(cur); cur = word; }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!cur.empty()) out.push_back(cur);
    for (const std::string& ln : out) {
        if (!typed) { printCentered(ln, cols); continue; }
        int pad = (std::max(cols, Console::cols()) - visibleLen(ln)) / 2;
        if (pad > 0) std::cout << std::string((size_t)pad, 0x20);
        typeWrite(ln + "\n");
    }
}

void UIHelper::printWrapped(const std::string& text, int indent, int hang, int measure) {
    const int room = std::max(24, std::min(measure, Console::cols() - indent - 2));
    std::vector<std::string> out;
    std::string cur;
    size_t i = 0;
    while (i <= text.size()) {
        size_t sp = text.find(' ', i);
        std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        if (cur.empty()) cur = word;
        else if (visibleLen(cur) + 1 + visibleLen(word) <= room) cur += " " + word;
        else { out.push_back(cur); cur = word; }
        if (sp == std::string::npos) break;
        i = sp + 1;
    }
    if (!cur.empty()) out.push_back(cur);
    for (size_t n = 0; n < out.size(); ++n)
        std::cout << std::string((size_t)(n == 0 ? indent : indent + hang), 0x20) << out[n] << "\n";
}

// Blank rows so a block of `lines` sits in the middle of the screen instead of
// hugging the top.
void UIHelper::padToCenter(int lines) {
    int pad = (Console::rows() - lines) / 2;
    for (int i = 0; i < pad; i++) std::cout << "\n";
}

namespace {
// The title and its menu are drawn, not printed. On the character grid a one
// character difference in label length can only move an option by a whole
// column or none at all, depending on the parity of the console width, so
// "Start Game" and "How to Play" could never sit correctly relative to each
// other. Drawn at pixel positions they simply centre.
bool gBannerOn = false;

// The headline overlay. Shares the modal renderer slot with the title
// banner and the card pickers, which never want it at the same time.
std::string gHeadline;
SDL_Color   gHeadlineCol{ 134, 209, 107, 255 };
std::vector<std::string> gTitleOpts;
int gTitleSel = 0;
std::vector<SDL_Rect> gTitleRects;

void layoutTitleMenu() {
    gTitleRects.clear();
    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());
    const int rowH = ch + ch / 2;
    int y = Platform::screenH() / 2 + ch;
    for (const std::string& o : gTitleOpts) {
        int w = (int)o.size() * cw;
        gTitleRects.push_back(SDL_Rect{ (Platform::screenW() - w) / 2, y, w, ch });
        y += rowH;
    }
}

// Defined below drawTitleBanner, which calls it from both its paths.
void drawTitleMenuRows();

void drawHeadline() {
    if (gHeadline.empty()) return;
    SDL_Renderer* r = Platform::renderer();
    const int w = (int)gHeadline.size() * Console::dispCellW();
    const int y = Platform::screenH() / 4;
    if (w > Platform::screenW() - 40) {
        // Too narrow for the display face: clipped text reads worse than
        // smaller text, so the widget face carries it instead.
        const int bw = (int)gHeadline.size() * Console::bigCellW();
        const int bx = (Platform::screenW() - bw) / 2;
        Console::drawTextBigPx(r, bx + 2, y + 2, gHeadline, SDL_Color{ 0, 0, 0, 255 }, true);
        Console::drawTextBigPx(r, bx, y, gHeadline, gHeadlineCol, true);
        return;
    }
    const int x = (Platform::screenW() - w) / 2;
    Console::drawTextDispPx(r, x + 3, y + 3, gHeadline, SDL_Color{ 6, 6, 9, 255 });
    Console::drawTextDispPx(r, x, y, gHeadline, gHeadlineCol);
}

void drawTitleBanner() {
    if (!gBannerOn) return;
    SDL_Renderer* r = Platform::renderer();
    const std::string title = "MOONSTRUCK";
    const int w = (int)title.size() * Console::dispCellW();
    const int y = Platform::screenH() / 5;
    if (w > Platform::screenW() - 40) {
        const int bw = (int)title.size() * Console::bigCellW();
        const int bx = (Platform::screenW() - bw) / 2;
        Console::drawTextBigPx(r, bx + 3, y + 3, title, SDL_Color{ 0, 0, 0, 255 }, true);
        Console::drawTextBigPx(r, bx, y, title, SDL_Color{ 240, 200, 60, 255 }, true);
        layoutTitleMenu();
        drawTitleMenuRows();
        return;
    }
    const int x = (Platform::screenW() - w) / 2;
    // Gold on a true black shadow. The shadow is offset a pixel further than
    // the headline's because a warm colour on a warm-lit backdrop needs more
    // separation than a cool one did.
    Console::drawTextDispPx(r, x + 4, y + 4, title, SDL_Color{ 0, 0, 0, 255 });
    Console::drawTextDispPx(r, x, y, title, SDL_Color{ 240, 200, 60, 255 });

    layoutTitleMenu();
    drawTitleMenuRows();
}

void drawTitleMenuRows() {
    SDL_Renderer* r = Platform::renderer();
    const int cw = std::max(1, Platform::cellW());
    for (size_t i = 0; i < gTitleRects.size(); i++) {
        const bool sel = ((int)i == gTitleSel);
        const SDL_Rect& q = gTitleRects[i];
        Console::drawTextPx(r, q.x, q.y, gTitleOpts[i],
                            sel ? SDL_Color{ 166, 226, 46, 255 }
                                : SDL_Color{ 208, 208, 222, 255 }, sel);
        if (sel) Console::drawTextPx(r, q.x - cw * 2, q.y, ">",
                                     SDL_Color{ 166, 226, 46, 255 }, true);
    }
}
} // namespace

void UIHelper::showTitleBanner(bool on) {
    gBannerOn = on;
    EnemyArt::setTitleMode(on);
    Platform::setModalRenderer(on ? std::function<void()>(&drawTitleBanner)
                                  : std::function<void()>());
}

void UIHelper::showHeadline(const std::string& text, int r, int g, int b) {
    gHeadline = text;
    gHeadlineCol = SDL_Color{ (Uint8)r, (Uint8)g, (Uint8)b, 255 };
    Platform::setModalRenderer(text.empty() ? std::function<void()>()
                                            : std::function<void()>(&drawHeadline));
}

void UIHelper::printTitle() {
    // Nothing is printed: the title and menu are both drawn. The tagline and
    // the five line summary were removed because the opening story beat tells
    // the same thing properly a moment later.
    showTitleBanner(true);
}

int UIHelper::titleMenu(const std::vector<std::string>& options) {
    if (options.empty()) return -1;
    gTitleOpts = options;
    gTitleSel = 0;
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);
    const int n = (int)options.size();

    while (true) {
        layoutTitleMenu();
        int mx, my;
        Platform::mousePos(mx, my);
        if (mx != lastMx || my != lastMy) {
            lastMx = mx; lastMy = my;
            for (int i = 0; i < n; i++) {
                const SDL_Rect& q = gTitleRects[i];
                if (mx >= q.x - 8 && mx < q.x + q.w + 8 && my >= q.y && my < q.y + q.h)
                    { gTitleSel = i; break; }
            }
        }
        Platform::KeyEvent k = Platform::pollKey();
        if (k.key == Platform::Key::UP || k.key == Platform::Key::LEFT)
            gTitleSel = (gTitleSel - 1 + n) % n;
        else if (k.key == Platform::Key::DOWN || k.key == Platform::Key::RIGHT)
            gTitleSel = (gTitleSel + 1) % n;
        else if (k.key == Platform::Key::ENTER) return gTitleSel;
        else if (k.key == Platform::Key::ESCAPE) return -1;

        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            for (int i = 0; i < n; i++) {
                const SDL_Rect& q = gTitleRects[i];
                if (cx >= q.x - 8 && cx < q.x + q.w + 8 && cy >= q.y && cy < q.y + q.h)
                    { gTitleSel = i; return i; }
            }
        }
        Platform::frame();
    }
}


void UIHelper::printGameOverScreen(bool won, int encountersWon, int cardsCollected) {
    std::cout << "\n";
    if (won) {
        std::cout << Color::BOLD << Color::GREEN;
        printLine(60, '=');
        printCentered("YOU WIN!", 60);
        printLine(60, '=');
        std::cout << Color::RESET;
    } else {
        std::cout << Color::BOLD << Color::RED;
        printLine(60, '=');
        printCentered("YOU LOST!", 60);
        printLine(60, '=');
        std::cout << Color::RESET;
    }
    std::cout << "\nRun Summary:\n";
    std::cout << "  Encounters Won:  " << Color::YELLOW << encountersWon  << Color::RESET << "\n";
    std::cout << "  Cards Collected: " << Color::CYAN   << cardsCollected << Color::RESET << "\n";
    printLine(60, '-');
}

void UIHelper::pause(int ms) {
    platSleep(ms);
}

// 100 is the rate every typeWrite() call was authored at. 0 means the text
// arrives whole, which is what an impatient second read of the story wants.
static int gTextSpeed = 100;
void UIHelper::setTextSpeed(int pct) { gTextSpeed = pct < 0 ? 0 : (pct > 400 ? 400 : pct); }
int  UIHelper::textSpeed() { return gTextSpeed; }

void UIHelper::clearScreen() {
    Console::clear();
}

int UIHelper::visibleLen(const std::string& s) {
    int len = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0x1B) {
            // ANSI escape: skip until final letter (inclusive)
            i++;
            while (i < s.size() && !std::isalpha((unsigned char)s[i])) i++;
            if (i < s.size()) i++;
        } else if (c >= 0xC0) {
            // UTF-8 multibyte sequence - counts as one visual cell
            len++;
            i++;
            while (i < s.size() && (unsigned char)s[i] >= 0x80 && (unsigned char)s[i] < 0xC0) i++;
        } else {
            len++;
            i++;
        }
    }
    return len;
}

void UIHelper::waitForKey(const std::string& prompt) {
    // The default footer is centred, because every screen that takes it is
    // centred now. A caller passing its own prompt is placing it itself: the
    // combat log wants its prompt inline with the log text, not in the middle.
    if (prompt == "  (press any key to continue)")
        printCentered(std::string("\033[2m") + "(press any key to continue)" + "\033[0m");
    else
        std::cout << "\033[2m" << prompt << "\033[0m";
    flushInputBuffer();
    // A click anywhere counts as "any key", matching the prompt's intent.
    while (true) {
        Platform::KeyEvent k = Platform::pollKey();
        if (k.key != Platform::Key::NONE) break;
        int cx, cy;
        if (Platform::takeClick(cx, cy)) break;
        Platform::frame();
    }
    std::cout << "\n";
}

// Centred menus are opt in: the battle and rest menus read better left
// aligned against their own content, but a full screen menu under a
// centred title looked detached hugging the left edge.

void UIHelper::typeWrite(const std::string& text, int msPerChar) {
    // Applied here rather than at the call sites, so every piece of typed
    // text in the game answers the setting without knowing about it.
    msPerChar = gTextSpeed <= 0 ? 0 : msPerChar * 100 / gTextSpeed;
    // Timing runs off a wall-clock deadline rather than sleeping per character.
    //
    // platSleep() draws at least one vsynced frame, so the old per-character
    // sleep couldn't take less than a frame (~16ms) no matter what was asked
    // for - the default 10ms/char actually ran ~60% slower than authored. Here
    // the deadline advances by exactly msPerChar per character and frames are
    // drawn only while there's time to spare, so several characters can land
    // in one frame and the text streams at the speed it was written for.
    Uint32 deadline = SDL_GetTicks();
    auto waitOne = [&]() {
        if (msPerChar <= 0) return;
        deadline += (Uint32)msPerChar;
        while ((Sint32)(deadline - SDL_GetTicks()) > 0) Platform::frame();
    };

    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\033' && i + 1 < text.size() && (unsigned char)text[i + 1] == '[') {
            // ANSI escape sequence - print the whole thing atomically (no delay).
            size_t j = i + 2;
            while (j < text.size() && !std::isalpha((unsigned char)text[j])) j++;
            if (j < text.size()) j++; // include the terminating letter
            for (size_t k = i; k < j; k++) std::cout << text[k];
            i = j;
        } else if (c >= 0xC0) {
            // UTF-8 multi-byte lead byte - print entire codepoint atomically.
            size_t j = i + 1;
            while (j < text.size() &&
                   (unsigned char)text[j] >= 0x80 &&
                   (unsigned char)text[j] <  0xC0) j++;
            for (size_t k = i; k < j; k++) std::cout << text[k];
            waitOne();
            i = j;
        } else {
            std::cout << (char)c;
            // Only delay on visible characters (not spaces, newlines, tabs).
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') waitOne();
            i++;
        }
    }
    Platform::frame(); // make sure the finished line is on screen
}
