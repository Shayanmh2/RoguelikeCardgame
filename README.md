# RoguelikeCardgame

A turn-based roguelike deckbuilder in C++, built on SDL2. Runs on Windows, macOS and Linux.

Fight your way through five zones and six bosses, building a deck out of what each
encounter drops. Every enemy has its own move set and its own weakness; the run ends
when you fall, but one card comes with you into the next one.

## Playing

Grab a build from the [Releases](../../releases) page, unzip it, and run the executable.
The `assets`, `config` and `sounds` folders need to sit next to it — the archive is
already laid out that way.

Arrow keys or the mouse to select, Enter to confirm.

## Building from source

Requires CMake 3.16+ and a C++17 compiler. SDL2, SDL2_ttf and SDL2_mixer are fetched and
built from source automatically, so there are no system packages to install on Windows or
macOS.

```
cmake -B build -S .
cmake --build build --config Release
```

On Linux, SDL needs the X11, OpenGL and audio development headers present before it
configures, or you get a build with no video backend:

```
sudo apt-get install -y libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
  libxi-dev libxfixes-dev libxss-dev libxkbcommon-dev libwayland-dev \
  libgl1-mesa-dev libegl1-mesa-dev libasound2-dev libpulse-dev
```

If CMake rejects a vendored dependency's `cmake_minimum_required`, set
`CMAKE_POLICY_VERSION_MINIMUM=3.5` in the environment. It has to be an environment
variable — passed as `-D` the value gets truncated and CMake rejects it.

The build copies `assets/`, `config/` and `sounds/` next to the binary, so the game is
runnable straight out of the build directory.

## How it's put together

The game logic is renderer-agnostic. `Game.cpp` and everything under it reach the outside
world through exactly four seams — `std::cout`, `UIHelper`, `EnemyArt` and `Audio` — so
moving off the terminal meant reimplementing those four rather than rewriting the rules.

- **`Console`** redirects `std::cout` into a virtual terminal: a cell grid with a parser
  for the ANSI escapes the game already emitted, drawn with SDL_ttf. Colors 0–15 use the
  Campbell palette so the game renders as it was authored; 16–255 use the xterm cube.
- **`Platform`** owns the window, the renderer and the event loop. The game still calls
  blocking functions like `pause()` and `menuSelect()`; those pump the SDL loop from the
  inside, so the window stays responsive without the game code knowing.
- **`EnemyArt`** draws the sprite sheets as GPU textures with the combat overlays
  composited on top, holding each pose for the same duration the terminal build paused for.

## Third-party

- [SDL2](https://libsdl.org), SDL2_ttf, SDL2_mixer — zlib license
- [stb_image](https://github.com/nothings/stb) — public domain / MIT
- DejaVu Sans Mono — see `assets/DejaVuSansMono-LICENSE.txt`

The terminal-only version this grew out of is preserved at the `v0.89---Terminal-Last` tag.
