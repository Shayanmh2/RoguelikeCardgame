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
#include <iostream>
#include <cmath>
#include <cctype>
#include <algorithm>

// Sleeping now pumps the SDL event/render loop instead of blocking the thread,
// so the window keeps drawing (and stays closable) during every game pause.
static void platSleep(int ms) { Platform::delay(ms); }
static void flushInputBuffer() { Platform::flushKeys(); }

// Shared input loop for both menus. The drawing side is untouched from the
// terminal build - it still reprints the block via DECRC - so this only has to
// decide which option is current and when to commit. optionRow maps each
// option to the console row it was drawn on, which makes the list clickable
// and hoverable in addition to arrow-key driven.
// `current` is taken by reference on purpose: each caller's printAll lambda
// captures that same variable to decide which row to highlight, so moving the
// selection has to write through to it. Taking it by value left the arrow keys
// updating a private copy that nothing ever drew - the selection looked stuck.
static int menuInputLoop(int n,
                         const std::function<bool(int)>& isDisabled,
                         const std::function<void()>& printAll,
                         int& current,
                         const std::vector<int>& optionRow,
                         const std::function<void()>& onIdleTick,
                         int idleTickMs) {
    auto step = [&](int dir) {
        int next = current;
        for (int i = 0; i < n; i++) {
            next = (next + dir + n) % n;
            if (!isDisabled(next)) break;
        }
        if (!isDisabled(next) && next != current) {
            current = next;
            std::cout << "\0338"; // DECRC - back to the block start, then reprint
            printAll();
        }
    };
    auto optionAtRow = [&](int row) {
        if (row < 0) return -1;
        for (int i = 0; i < n; i++)
            if (optionRow[i] >= 0 && optionRow[i] == row && !isDisabled(i)) return i;
        return -1;
    };

    flushInputBuffer();
    Uint32 lastTick = SDL_GetTicks();

    // Hover only re-targets when the mouse actually moves. Sampling it every
    // frame instead made the arrow keys look broken: moving the selection with
    // the keyboard was immediately overwritten by whatever row the (stationary)
    // cursor happened to be resting on.
    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);

    while (true) {
        int mx, my;
        Platform::mousePos(mx, my);
        if (mx != lastMx || my != lastMy) {
            lastMx = mx; lastMy = my;
            int hovered = optionAtRow(Console::rowAtY(my));
            if (hovered >= 0 && hovered != current) {
                current = hovered;
                std::cout << "\0338";
                printAll();
            }
        }

        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            int clicked = optionAtRow(Console::rowAtY(cy));
            if (clicked >= 0) { std::cout << "\n"; return clicked; }
        }

        Platform::KeyEvent k = Platform::pollKey();
        switch (k.key) {
            case Platform::Key::UP:
            case Platform::Key::LEFT:  step(-1); break;
            case Platform::Key::DOWN:
            case Platform::Key::RIGHT: step(+1); break;
            case Platform::Key::ENTER:
                if (!isDisabled(current)) { std::cout << "\n"; return current; }
                break;
            case Platform::Key::ESCAPE: return -1;
            default: break;
        }

        if (onIdleTick && SDL_GetTicks() - lastTick >= (Uint32)idleTickMs) {
            onIdleTick();
            lastTick = SDL_GetTicks();
        }
        Platform::frame();
    }
}

void UIHelper::printLine(int width, char c) {
    std::cout << Color::DIM;
    for (int i = 0; i < width; ++i) std::cout << c;
    std::cout << Color::RESET << "\n";
}

std::string UIHelper::createHealthBar(int current, int max, int width) {
    if (max <= 0) return "";

    int filled = static_cast<int>((static_cast<double>(current) / max) * width);
    if (current > 0 && filled == 0) filled = 1;

    const char* color = hpColor(current, max);
    std::string bar = std::string(color) + "[";
    for (int i = 0; i < width; ++i)
        bar += (i < filled) ? "\xe2\x96\x88" : " "; // UTF-8 █
    bar += "]";
    bar += Color::RESET;
    return bar;
}

std::string UIHelper::createArmorBar(int armor, int width) {
    if (armor < 0) armor = 0;
    int filled = std::min(armor / 2, width);
    std::string bar = std::string(Color::BLUE) + "[";
    for (int i = 0; i < width; ++i)
        bar += (i < filled) ? "\xe2\x96\xa0" : " "; // UTF-8 ■
    bar += "]";
    bar += Color::RESET;
    return bar;
}

void UIHelper::printCentered(const std::string& text, int width) {
    int padding = (width - (int)text.length()) / 2;
    if (padding < 0) padding = 0;
    std::cout << std::string(padding, ' ') << text << "\n";
}

void UIHelper::printEncounterHeader(int encounterNum, const std::string& difficulty, const std::string& tierLabel) {
    // Pick difficulty color
    const char* diffColor = Color::GREEN;
    if      (difficulty == "NORMAL")     diffColor = Color::YELLOW;
    else if (difficulty == "HARD")       diffColor = Color::YELLOW;
    else if (difficulty == "NIGHTMARE")  diffColor = Color::RED;
    else if (difficulty == "IMPOSSIBLE") diffColor = Color::MAGENTA;
    else if (difficulty == "INSANE")     diffColor = Color::MAGENTA;

    printLine(60, '=');
    std::cout << "  " << Color::BOLD << Color::CYAN << "ENCOUNTER " << encounterNum << Color::RESET
              << " | " << diffColor << difficulty << Color::RESET
              << " [" << Color::DIM << tierLabel << Color::RESET << "]\n";
    printLine(60, '-');
}

void UIHelper::printBossHeader(int encounterNum, const std::string& bossName) {
    std::cout << Color::BOLD << Color::MAGENTA;
    printLine(60, '*');
    printCentered("!!! BOSS ENCOUNTER !!!", 60);
    printCentered("Encounter " + std::to_string(encounterNum), 60);
    printLine(60, '*');
    printCentered(bossName, 60);
    printLine(60, '*');
    std::cout << Color::RESET;
}

void UIHelper::printCombatStatus(int playerHP, int playerMaxHP, int playerArmor, int playerEnergy, int maxEnergy,
                                  const std::string& enemyName, int enemyHP, int enemyMaxHP, int enemyArmor,
                                  int enemyAttack, int enemyDefense) {
    printLine(60, '-');

    // Player
    std::cout << Color::BOLD << Color::WHITE << "PLAYER" << Color::RESET << "\n";
    std::cout << "  HP:  "
              << hpColor(playerHP, playerMaxHP) << playerHP << "/" << playerMaxHP << Color::RESET
              << "  " << createHealthBar(playerHP, playerMaxHP) << "\n";
    std::cout << "  ARM: "
              << Color::ARMOR_CLR << playerArmor << Color::RESET
              << "  " << createArmorBar(playerArmor) << "\n";
    std::cout << "  ENERGY: "
              << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy << Color::RESET << "\n\n";

    // Enemy - ARM and DEF are shown as one combined DEF figure (both are flat
    // damage reduction; showing them separately just for the enemy read as confusing)
    std::cout << Color::BOLD << Color::RED << "ENEMY: " << enemyName << Color::RESET << "\n";
    std::cout << "  HP:  "
              << hpColor(enemyHP, enemyMaxHP) << enemyHP << "/" << enemyMaxHP << Color::RESET
              << "  " << createHealthBar(enemyHP, enemyMaxHP) << "\n";
    std::cout << "  ATK: " << Color::RED    << enemyAttack  << Color::RESET
              << " | DEF: " << Color::BLUE  << (enemyDefense + enemyArmor) << Color::RESET << "\n";

    printLine(60, '-');
}

void UIHelper::printTitle() {
    std::cout << "\n";
    std::cout << Color::BOLD << Color::CYAN;
    printLine(60, '=');
    printCentered("ROGUELIKE CARDGAME", 60);
    std::cout << Color::RESET << Color::DIM;
    printCentered("A turn-based deckbuilder roguelike", 60);
    std::cout << Color::RESET << Color::BOLD << Color::CYAN;
    printLine(60, '=');
    std::cout << Color::RESET << "\n" << Color::DIM;
    printCentered("Stripped of his soul in a battle long forgotten,", 60);
    printCentered("a lone knight descends into the dark to reclaim it.", 60);
    printCentered("Each foe felled returns a fragment of who he was.", 60);
    printCentered("At the depths below waits his own shadow,", 60);
    printCentered("the last piece he must face to become a legend.", 60);
    std::cout << Color::RESET << "\n";
    std::cout << Color::DIM << "Use arrow keys to navigate, Enter to select.\n\n" << Color::RESET;
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

int UIHelper::getCursorRow() {
    return Console::cursorRow();
}

void UIHelper::setCursorRow(int row) {
    Console::setCursorRow(row);
}

int UIHelper::menuSelectRight(const std::vector<std::string>& leftLines,
                               const std::vector<int>&         optionIndices,
                               const std::vector<std::string>& options,
                               int leftColWidth,
                               int startIndex,
                               const std::vector<bool>& disabled,
                               const std::function<void()>& onIdleTick,
                               int idleTickMs) {
    int n = (int)options.size();
    if (n == 0) return -1;

    auto isDisabled = [&](int i) -> bool {
        return !disabled.empty() && i < (int)disabled.size() && disabled[i];
    };

    int current = (startIndex >= 0 && startIndex < n) ? startIndex : 0;
    while (current < n && isDisabled(current)) current++;
    if (current >= n) current = 0;

    // Merge leftLines+optionIndices, then append uncovered options as left-aligned footer rows
    std::vector<std::string> allLeft(leftLines);
    std::vector<int>         allOpt(optionIndices);
    std::vector<bool>        isFooter(leftLines.size(), false);

    // Pad to same size
    while ((int)allLeft.size() < (int)allOpt.size()) { allLeft.push_back(""); isFooter.push_back(false); }
    while ((int)allOpt.size()  < (int)allLeft.size()) allOpt.push_back(-1);

    // Append lines for options not yet mapped - these render left-aligned (no column padding)
    std::vector<bool> covered(n, false);
    for (int idx : allOpt) if (idx >= 0 && idx < n) covered[idx] = true;
    for (int i = 0; i < n; i++) {
        if (!covered[i]) {
            allLeft.push_back("");
            allOpt.push_back(i);
            isFooter.push_back(true);
        }
    }

    int totalLines = (int)allLeft.size();

    // Redraw via terminal-native save/restore cursor (DECSC/DECRC) rather than
    // computing a row count ourselves - avoids drift under ConPTY terminals.
    auto printAll = [&]() {
        for (int i = 0; i < totalLines; i++) {
            std::cout << "\033[2K\r";

            int optIdx = allOpt[i];
            bool dis = (optIdx >= 0 && optIdx < n) && isDisabled(optIdx);
            std::string rendered;

            if (isFooter[i] && optIdx >= 0 && optIdx < n) {
                // Left-aligned action row - no column padding
                if (optIdx == current)
                    rendered = " " + std::string(Color::SELECT_CLR) + "> " + options[optIdx] + "\033[0m";
                else if (dis)
                    rendered = "   \033[2m" + options[optIdx] + "\033[0m";
                else
                    rendered = "   " + options[optIdx];
            } else {
                rendered = allLeft[i];
                int vlen = visibleLen(allLeft[i]);
                int pad = leftColWidth - vlen;
                if (pad > 0) rendered += std::string(pad, ' ');

                if (optIdx >= 0 && optIdx < n) {
                    if (optIdx == current)
                        rendered += " " + std::string(Color::SELECT_CLR) + "> " + options[optIdx] + "\033[0m";
                    else if (dis)
                        rendered += "   \033[2m" + options[optIdx] + "\033[0m";
                    else
                        rendered += "   " + options[optIdx];
                }
            }

            std::cout << rendered << "\n";
        }
        std::cout.flush();
    };

    int blockStart = Console::cursorRow();
    std::cout << "\0337"; // DECSC - remember exactly where this menu block starts
    printAll();

    // Which console row each option landed on, for mouse hit-testing.
    std::vector<int> optionRow(n, -1);
    for (int i = 0; i < totalLines; i++) {
        int optIdx = allOpt[i];
        if (optIdx >= 0 && optIdx < n) optionRow[optIdx] = blockStart + i;
    }

    return menuInputLoop(n, isDisabled, printAll, current, optionRow, onIdleTick, idleTickMs);
}

int UIHelper::menuSelect(const std::vector<std::string>& options, int startIndex,
                         const std::vector<bool>& disabled) {
    if (options.empty()) return -1;

    int n = (int)options.size();

    auto isDisabled = [&](int i) -> bool {
        return !disabled.empty() && i < (int)disabled.size() && disabled[i];
    };

    // Find first enabled item starting from startIndex
    int current = (startIndex >= 0 && startIndex < n) ? startIndex : 0;
    while (current < n && isDisabled(current)) current++;
    if (current >= n) current = 0;

    // See menuSelectRight for why this uses DECSC/DECRC (terminal-native
    // save/restore cursor) instead of computing a row count ourselves.
    auto printOptions = [&]() {
        for (int i = 0; i < n; i++) {
            std::cout << "\033[2K\r";
            std::string rendered;
            if (i == current) {
                rendered = std::string(Color::SELECT_CLR) + "> " + options[i] + "\033[0m";
            } else if (isDisabled(i)) {
                rendered = "  \033[2m" + options[i] + "\033[0m";
            } else {
                rendered = "  " + options[i];
            }
            std::cout << rendered << "\n";
        }
        std::cout.flush();
    };

    int blockStart = Console::cursorRow();
    std::cout << "\0337"; // DECSC - remember exactly where this menu block starts
    printOptions();

    std::vector<int> optionRow(n);
    for (int i = 0; i < n; i++) optionRow[i] = blockStart + i;

    return menuInputLoop(n, isDisabled, printOptions, current, optionRow, nullptr, 0);
}

void UIHelper::typeWrite(const std::string& text, int msPerChar) {
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
