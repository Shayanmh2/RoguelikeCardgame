#include "UIHelper.h"
#include "Colors.h"
#include <iostream>
#include <cmath>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <conio.h>
static void platSleep(int ms) { Sleep(ms); }
// Drop buffered keypresses so a menu's first _getch() reads a fresh key, not a stale one
static void flushInputBuffer() { while (_kbhit()) _getch(); }
#else
#include <thread>
#include <chrono>
static void platSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
static void flushInputBuffer() {}
#endif

void UIHelper::printLine(int width, char c) {
    std::cout << Color::DIM;
    for (int i = 0; i < width; ++i) std::cout << c;
    std::cout << Color::RESET << "\n";
}

void UIHelper::printBox(const std::string& title, int width) {
    printLine(width, '=');
    if (!title.empty()) {
        int padding = (width - (int)title.length() - 2) / 2;
        if (padding < 0) padding = 0;
        std::cout << std::string(padding, ' ')
                  << Color::BOLD << Color::CYAN << title << Color::RESET << "\n";
        printLine(width, '=');
    }
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
    std::cout << "  PLAYS: "
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
    std::cout << Color::RESET;
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
    std::cout << "\033[2J\033[H";
    std::cout.flush();
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
    std::cout.flush();
#ifdef _WIN32
    flushInputBuffer();
    _getch();
#else
    if (std::cin.peek() == '\n') std::cin.ignore();
    std::cin.get();
#endif
    std::cout << "\n";
}

int UIHelper::menuSelectRight(const std::vector<std::string>& leftLines,
                               const std::vector<int>&         optionIndices,
                               const std::vector<std::string>& options,
                               int leftColWidth,
                               int startIndex,
                               const std::vector<bool>& disabled) {
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
                    rendered = " \033[1;36m> " + options[optIdx] + "\033[0m";
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
                        rendered += " \033[1;36m> " + options[optIdx] + "\033[0m";
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

    std::cout << "\0337"; // DECSC - remember exactly where this menu block starts
    printAll();
    flushInputBuffer();

    while (true) {
#ifdef _WIN32
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int dir = _getch();
            if (dir == 72) {  // up
                int next = current - 1;
                while (next >= 0 && isDisabled(next)) next--;
                if (next >= 0) {
                    current = next;
                    std::cout << "\0338"; // DECRC - jump back to the saved start, then reprint
                    printAll();
                }
            } else if (dir == 80) {  // down
                int next = current + 1;
                while (next < n && isDisabled(next)) next++;
                if (next < n) {
                    current = next;
                    std::cout << "\0338";
                    printAll();
                }
            }
        } else if (ch == 13) {
            std::cout << "\n";
            return current;
        } else if (ch == 27) {
            return -1;
        } else if (ch == 'H') {  // Shift+H - manual refresh if the screen ever looks glitched
            clearScreen();
            printAll();
        }
#else
        std::string line;
        if (!std::getline(std::cin, line)) return -1;
        if (line.empty() && !isDisabled(current)) return current;
        try {
            int idx = std::stoi(line) - 1;
            if (idx >= 0 && idx < n && !isDisabled(idx)) return idx;
        } catch (...) {}
#endif
    }
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
                rendered = "\033[1;36m> " + options[i] + "\033[0m";
            } else if (isDisabled(i)) {
                rendered = "  \033[2m" + options[i] + "\033[0m";
            } else {
                rendered = "  " + options[i];
            }
            std::cout << rendered << "\n";
        }
        std::cout.flush();
    };

    std::cout << "\0337"; // DECSC - remember exactly where this menu block starts
    printOptions();
    flushInputBuffer();

    while (true) {
#ifdef _WIN32
        int ch = _getch();
        if (ch == 0 || ch == 224) {
            int dir = _getch();
            if (dir == 72) {  // up
                int next = current - 1;
                while (next >= 0 && isDisabled(next)) next--;
                if (next >= 0) {
                    current = next;
                    std::cout << "\0338"; // DECRC - jump back to the saved start, then reprint
                    printOptions();
                }
            } else if (dir == 80) {  // down
                int next = current + 1;
                while (next < n && isDisabled(next)) next++;
                if (next < n) {
                    current = next;
                    std::cout << "\0338";
                    printOptions();
                }
            }
        } else if (ch == 13) {  // Enter
            std::cout << "\n";
            return current;
        } else if (ch == 27) {  // ESC
            return -1;
        } else if (ch == 'H') {  // Shift+H - manual refresh if the screen ever looks glitched
            clearScreen();
            printOptions();
        }
#else
        std::string line;
        if (!std::getline(std::cin, line)) return -1;
        if (line.empty()) {
            if (!isDisabled(current)) return current;
            continue;
        }
        try {
            int idx = std::stoi(line) - 1;
            if (idx >= 0 && idx < n && !isDisabled(idx)) return idx;
        } catch (...) {}
#endif
    }
}

void UIHelper::typeWrite(const std::string& text, int msPerChar) {
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];

        if (c == '\033' && i + 1 < text.size() && (unsigned char)text[i + 1] == '[') {
            // ANSI escape sequence - print the whole thing atomically (no delay).
            size_t j = i + 2;
            while (j < text.size() && !std::isalpha((unsigned char)text[j])) j++;
            if (j < text.size()) j++; // include the terminating letter
            for (size_t k = i; k < j; k++) std::cout << text[k];
            std::cout.flush();
            i = j;
        } else if (c >= 0xC0) {
            // UTF-8 multi-byte lead byte - print entire codepoint atomically.
            size_t j = i + 1;
            while (j < text.size() &&
                   (unsigned char)text[j] >= 0x80 &&
                   (unsigned char)text[j] <  0xC0) j++;
            for (size_t k = i; k < j; k++) std::cout << text[k];
            std::cout.flush();
            if (msPerChar > 0) platSleep(msPerChar);
            i = j;
        } else {
            std::cout << (char)c;
            std::cout.flush();
            // Only delay on visible characters (not spaces, newlines, tabs).
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t' && msPerChar > 0)
                platSleep(msPerChar);
            i++;
        }
    }
}
