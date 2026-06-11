#include "Game.h"
#include "Audio.h"
#ifdef _WIN32
#include <windows.h>
#endif

static void setupWindow() {
#ifdef _WIN32
    // UTF-8 output so block characters render correctly.
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    // Enable ANSI escape codes.
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    // Window title.
    SetConsoleTitleA("Roguelike Cardgame");

    // Font: Consolas 18pt — clean, supports Unicode block chars.
    CONSOLE_FONT_INFOEX fontEx = {};
    fontEx.cbSize    = sizeof(fontEx);
    fontEx.dwFontSize.Y = 18;
    fontEx.FontWeight   = 400;
    wcscpy_s(fontEx.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hOut, FALSE, &fontEx);

    // Buffer large so scroll history exists, window sized to 100×42.
    // Must set buffer before shrinking the window.
    COORD bufSize = {100, 2000};
    SetConsoleScreenBufferSize(hOut, bufSize);
    SMALL_RECT winRect = {0, 0, 99, 41};
    SetConsoleWindowInfo(hOut, TRUE, &winRect);
#endif
}

int main() {
    setupWindow();
    Audio::playBGM();   // looks for bgm.wav or bgm.mp3 next to the exe

    Game game;
    game.run();

    Audio::stopBGM();
    return 0;
}
