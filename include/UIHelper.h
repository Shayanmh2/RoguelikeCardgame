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

    // Left-aligned wrapping for text that belongs in the log box rather than
    // on a screen of its own. Wraps to the window, counts colour codes as
    // zero width, and indents every line after the first by `hang`.
    static void printWrapped(const std::string& text, int indent = 2, int hang = 2, int measure = 96);
    // The same wrap, handed back as rows instead of printed, for text that is
    // typed out: the log keeps each row as it arrives and never re-wraps it.
    static std::vector<std::string> wrapRows(const std::string& text, int room = 96);
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

    // Typing speed as a percentage of the authored rate: 100 is as written,
    // higher types faster, 0 prints whole lines at once. Set from Settings.
    static void setTextSpeed(int pct);

    // Sleep for ms milliseconds (cross-platform wrapper).
    static void pause(int ms);


    // Clear the terminal screen.
    static void clearScreen();

    // Measure the visible (printable) length of a string, ignoring ANSI escape sequences.
    static int visibleLen(const std::string& s);

    // Print a prompt and wait for any keypress before continuing.
    static void waitForKey(const std::string& prompt = "  (press any key to continue)");
};
