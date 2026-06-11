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
echo  drop audio files in there (.wav or .mp3):
echo    bgm      - background music (loops)
echo    attack   - player plays an attack card
echo    dead     - enemy killed
echo    hit      - enemy hits you
echo    defend   - player plays a defend card
echo    special  - status effect applied
echo    boss     - boss encounter begins
echo    win      - encounter won
echo    lose     - game over
echo    heal     - rest site heal
echo    upgrade  - card upgrade / forge
echo ================================
echo.
pause
