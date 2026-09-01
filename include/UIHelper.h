#pragma once

#include <string>
#include <vector>
#include <functional>

class UIHelper {
public:
    // Draw decorative elements
    static void printLine(int width = 60, char c = '-');


    // Centered text
    static void printCentered(const std::string& text, int width = 60);

    // Wraps to a readable measure, then centres each line on the window.
    static void printCenteredWrapped(const std::string& text, int measure = 72, bool typed = false);
    // Blank rows so a block of that many lines sits vertically centred.
    static void padToCenter(int lines);

    // The drawn title. Shown on the title screen, hidden anywhere else that
    // takes over the full screen.
    static void showTitleBanner(bool on);

    // One line of display-face text drawn over the console, centred near the
    // top. For moments that want to be an event rather than a log line.
    // An empty string takes it back down.
    static void showHeadline(const std::string& text, int r, int g, int b);

    // The title screen's own menu, drawn at pixel positions so the options
    // centre properly instead of snapping to character columns.
    static int titleMenu(const std::vector<std::string>& options);


    // Title screen
    static void printTitle();
    static void printGameOverScreen(bool won, int encountersWon, int cardsCollected);

    // Animated output - handles ANSI codes and UTF-8 characters correctly.
    // msPerChar=0 prints instantly (useful for toggling from call sites).
    static void typeWrite(const std::string& text, int msPerChar = 10);

    // Sleep for ms milliseconds (cross-platform wrapper).
    static void pause(int ms);


    // Clear the terminal screen.
    static void clearScreen();

    // Measure the visible (printable) length of a string, ignoring ANSI escape sequences.
    static int visibleLen(const std::string& s);

    // Print a prompt and wait for any keypress before continuing.
    static void waitForKey(const std::string& prompt = "  (press any key to continue)");
};
