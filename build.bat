@echo off
echo ================================
echo  Building Roguelike Cardgame...
echo ================================

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    pause
    exit /b 1
)

cmake --build build --config Release
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    pause
    exit /b 1
)

echo.
echo ================================
echo  Build successful!
echo  cardgame.exe is in this folder.
echo  Drop bgm.wav or bgm.mp3 here for music.
echo ================================
echo.
pause
