#include "UIHelper.h"
#include "Colors.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <conio.h>
static void platSleep(int ms) { Sleep(ms); }
#else
#include <thread>
#include <chrono>
static void platSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
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

void UIHelper::printBoxEnd(int width) {
    printLine(width, '=');
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

void UIHelper::printStatusHeader() {
    printBox("ROGUELIKE CARDGAME", 60);
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

    // Enemy
    std::cout << Color::BOLD << Color::RED << "ENEMY: " << enemyName << Color::RESET << "\n";
    std::cout << "  HP:  "
              << hpColor(enemyHP, enemyMaxHP) << enemyHP << "/" << enemyMaxHP << Color::RESET
              << "  " << createHealthBar(enemyHP, enemyMaxHP) << "\n";
    std::cout << "  ARM: "
              << Color::ARMOR_CLR << enemyArmor << Color::RESET
              << "  " << createArmorBar(enemyArmor) << "\n";
    std::cout << "  ATK: " << Color::RED    << enemyAttack  << Color::RESET
              << " | DEF: " << Color::BLUE  << enemyDefense << Color::RESET << "\n";

    printLine(60, '-');
}

void UIHelper::printCardHeader(int count) {
    printBox("HAND (" + std::to_string(count) + " cards)", 60);
}

void UIHelper::printCardRow(int index, const std::string& type, const std::string& name,
                             int cost, int value, const std::string& description) {
    // Pick color for card type tag
    const char* typeColor = Color::WHITE;
    if      (type == "ATTACK")  typeColor = Color::CARD_ATTACK;
    else if (type == "DEFEND")  typeColor = Color::CARD_DEFEND;
    else if (type == "SPECIAL") typeColor = Color::CARD_SPECIAL;

    const char* valLabel = (type == "ATTACK") ? "DMG"
                        : (type == "DEFEND") ? "ARM" : "STK";
    std::cout << "  " << Color::DIM << index << "." << Color::RESET
              << " [" << typeColor << std::setw(6) << std::left << type << Color::RESET << "] "
              << Color::BOLD << Color::CARD_NAME << std::setw(18) << std::left << name << Color::RESET
              << " (Cost: " << Color::ENERGY_CLR << cost << Color::RESET
              << ", " << valLabel << ": " << Color::GREEN << value << Color::RESET << ")\n";
    std::cout << "     " << Color::DIM << description << Color::RESET << "\n\n";
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
            // UTF-8 multibyte sequence — counts as one visual cell
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

    // Merge leftLines+optionIndices, then append uncovered options with blank left sides
    std::vector<std::string> allLeft(leftLines);
    std::vector<int>         allOpt(optionIndices);

    // Pad to same size
    while ((int)allLeft.size() < (int)allOpt.size()) allLeft.push_back("");
    while ((int)allOpt.size()  < (int)allLeft.size()) allOpt.push_back(-1);

    // Append lines for options not yet mapped
    std::vector<bool> covered(n, false);
    for (int idx : allOpt) if (idx >= 0 && idx < n) covered[idx] = true;
    for (int i = 0; i < n; i++) {
        if (!covered[i]) {
            allLeft.push_back("");
            allOpt.push_back(i);
        }
    }

    int totalLines = (int)allLeft.size();

    auto printAll = [&]() {
        for (int i = 0; i < totalLines; i++) {
            std::cout << "\033[2K\r";
            std::cout << allLeft[i];

            int vlen = visibleLen(allLeft[i]);
            int pad = leftColWidth - vlen;
            if (pad > 0) std::cout << std::string(pad, ' ');

            int optIdx = allOpt[i];
            if (optIdx >= 0 && optIdx < n) {
                bool dis = isDisabled(optIdx);
                if (optIdx == current) {
                    std::cout << " \033[1;36m> " << options[optIdx] << "\033[0m";
                } else if (dis) {
                    std::cout << "   \033[2m" << options[optIdx] << "\033[0m";
                } else {
                    std::cout << "   " << options[optIdx];
                }
            }
            std::cout << "\n";
        }
        std::cout.flush();
    };

    printAll();

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
                    std::cout << "\033[" << totalLines << "A";
                    printAll();
                }
            } else if (dir == 80) {  // down
                int next = current + 1;
                while (next < n && isDisabled(next)) next++;
                if (next < n) {
                    current = next;
                    std::cout << "\033[" << totalLines << "A";
                    printAll();
                }
            }
        } else if (ch == 13) {
            std::cout << "\n";
            return current;
        } else if (ch == 27) {
            return -1;
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

    auto printOptions = [&]() {
        for (int i = 0; i < n; i++) {
            std::cout << "\033[2K\r";
            if (i == current) {
                std::cout << "\033[1;36m> " << options[i] << "\033[0m";
            } else if (isDisabled(i)) {
                std::cout << "  \033[2m" << options[i] << "\033[0m";
            } else {
                std::cout << "  " << options[i];
            }
            std::cout << "\n";
        }
        std::cout.flush();
    };

    printOptions();

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
                    std::cout << "\033[" << n << "A";
                    printOptions();
                }
            } else if (dir == 80) {  // down
                int next = current + 1;
                while (next < n && isDisabled(next)) next++;
                if (next < n) {
                    current = next;
                    std::cout << "\033[" << n << "A";
                    printOptions();
                }
            }
        } else if (ch == 13) {  // Enter
            std::cout << "\n";
            return current;
        } else if (ch == 27) {  // ESC
            return -1;
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
            // ANSI escape sequence — print the whole thing atomically (no delay).
            size_t j = i + 2;
            while (j < text.size() && !std::isalpha((unsigned char)text[j])) j++;
            if (j < text.size()) j++; // include the terminating letter
            for (size_t k = i; k < j; k++) std::cout << text[k];
            std::cout.flush();
            i = j;
        } else if (c >= 0xC0) {
            // UTF-8 multi-byte lead byte — print entire codepoint atomically.
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
