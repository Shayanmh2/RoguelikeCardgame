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
echo.
echo  Create a sounds\ folder next to the exe and
echo  drop audio files in there:
echo    sounds\bgm.wav   (or bgm.mp3)  - background music
echo    sounds\attack.wav              - player attack
echo    sounds\hit.wav                 - taking damage
echo    sounds\win.wav                 - encounter won
echo    sounds\lose.wav                - game over
echo    sounds\heal.wav                - rest site heal
echo    sounds\upgrade.wav             - card upgrade
echo ================================
echo.
pause
