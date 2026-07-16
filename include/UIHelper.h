#pragma once

#include <string>
#include <vector>
#include <functional>

class UIHelper {
public:
    // Draw decorative elements
    static void printLine(int width = 60, char c = '-');

    // Health bar visualization
    static std::string createHealthBar(int current, int max, int width = 20);
    static std::string createArmorBar(int armor, int width = 15);

    // Centered text
    static void printCentered(const std::string& text, int width = 60);

    // Status display helpers
    static void printEncounterHeader(int encounterNum, const std::string& difficulty, const std::string& tierLabel);
    static void printBossHeader(int encounterNum, const std::string& bossName);
    static void printCombatStatus(int playerHP, int playerMaxHP, int playerArmor, int playerEnergy, int maxEnergy,
                                   const std::string& enemyName, int enemyHP, int enemyMaxHP, int enemyArmor,
                                   int enemyAttack, int enemyDefense);

    // Title screen
    static void printTitle();
    static void printGameOverScreen(bool won, int encountersWon, int cardsCollected);

    // Animated output - handles ANSI codes and UTF-8 characters correctly.
    // msPerChar=0 prints instantly (useful for toggling from call sites).
    static void typeWrite(const std::string& text, int msPerChar = 10);

    // Sleep for ms milliseconds (cross-platform wrapper).
    static void pause(int ms);

    // Arrow-key menu: prints options, lets user navigate with up/down, confirms with Enter.
    // Returns the selected index, or -1 if ESC is pressed.
    // disabled[i] = true grays out that option and skips it during navigation.
    static int menuSelect(const std::vector<std::string>& options, int startIndex = 0,
                          const std::vector<bool>& disabled = {});

    // Side-by-side menu: prints leftLines as the left column (padded to leftColWidth visual chars),
    // and options as the right column. optionIndices[i] maps each left line to an option index
    // (or -1 for lines with no corresponding option). Options not referenced get blank left sides.
    // onIdleTick, if set, is called every idleTickMs while waiting on a keypress (Windows only) -
    // used to redraw an idle animation elsewhere on screen without blocking input.
    static int menuSelectRight(const std::vector<std::string>& leftLines,
                               const std::vector<int>&         optionIndices,
                               const std::vector<std::string>& options,
                               int leftColWidth,
                               int startIndex = 0,
                               const std::vector<bool>& disabled = {},
                               const std::function<void()>& onIdleTick = nullptr,
                               int idleTickMs = 450);

    // Clear the terminal screen.
    static void clearScreen();

    // Current 0-based cursor row in the console screen buffer (Windows only; returns 0 elsewhere).
    static int getCursorRow();

    // Moves the cursor to column 0 of the given 0-based row (Windows only; no-op elsewhere).
    static void setCursorRow(int row);

    // Measure the visible (printable) length of a string, ignoring ANSI escape sequences.
    static int visibleLen(const std::string& s);

    // Print a prompt and wait for any keypress before continuing.
    static void waitForKey(const std::string& prompt = "  (press any key to continue)");
};
