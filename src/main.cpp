#include "Game.h"
#include "Audio.h"
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#endif

static void setupWindow() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;

    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode))
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    SetConsoleTitleA("Roguelike Cardgame");

    CONSOLE_FONT_INFOEX fontEx = {};
    fontEx.cbSize       = sizeof(fontEx);
    fontEx.dwFontSize.Y = 18;
    fontEx.FontWeight   = 400;
    wcscpy_s(fontEx.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hOut, FALSE, &fontEx);

    HWND hwnd = GetConsoleWindow();
    char cls[64] = {};
    if (hwnd) GetClassNameA(hwnd, cls, sizeof(cls));
    if (hwnd && strcmp(cls, "ConsoleWindowClass") == 0) {
        // Classic console host: maximize. The buffer must be at least as
        // wide as the window, so oversize it first, then trim it to the
        // real column count so no horizontal scrollbar remains.
        SetConsoleScreenBufferSize(hOut, COORD{250, 2000});
        ShowWindow(hwnd, SW_MAXIMIZE);
        CONSOLE_SCREEN_BUFFER_INFO info = {};
        if (GetConsoleScreenBufferInfo(hOut, &info)) {
            SHORT cols = (SHORT)(info.srWindow.Right - info.srWindow.Left + 1);
            if (cols > 0) SetConsoleScreenBufferSize(hOut, COORD{cols, 2000});
        }
    } else {
        // Windows Terminal ignores Win32 sizing; ask it to resize via the
        // xterm escape (honored by recent versions, ignored elsewhere).
        DWORD written = 0;
        const char* resizeSeq = "\x1b[8;52;190t";
        WriteConsoleA(hOut, resizeSeq, (DWORD)strlen(resizeSeq), &written, NULL);
    }
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
