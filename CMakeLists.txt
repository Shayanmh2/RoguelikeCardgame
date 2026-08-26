cmake_minimum_required(VERSION 3.20)
project(cardgame_sdl2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.9
    GIT_SHALLOW TRUE
)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(
    SDL2_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-2.22.0
    GIT_SHALLOW TRUE
)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2_ttf)

FetchContent_Declare(
    SDL2_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-2.8.0
    GIT_SHALLOW TRUE
)
set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2_mixer)

add_executable(cardgame_sdl2 WIN32
    src/main.cpp
    # Platform layer: the SDL2 replacements for the terminal I/O the game
    # was originally written against.
    src/Platform.cpp
    src/Console.cpp
    src/UIHelper.cpp
    src/EnemyArt.cpp
    src/Audio.cpp
    # Game logic, unchanged from the terminal build.
    src/Game.cpp
    src/Card.cpp
    src/Deck.cpp
    src/Enemy.cpp
    src/StatusEffect.cpp
    src/ConfigLoader.cpp
    src/Run.cpp
    src/RunStats.cpp
    src/RewardPool.cpp
    src/Upgrade.cpp
    src/UpgradeSystem.cpp
)
target_include_directories(cardgame_sdl2 PRIVATE include)
target_link_libraries(cardgame_sdl2 PRIVATE
    SDL2::SDL2-static SDL2::SDL2main
    SDL2_ttf::SDL2_ttf-static
    SDL2_mixer::SDL2_mixer-static)

# Wipe before copying: copy_directory leaves files that were deleted from the
# source tree, which would quietly package removed assets into a release. That
# matters for anything license-bound (a font we stopped shipping shouldn't
# survive in the zip just because it was there on an earlier build).
add_custom_command(TARGET cardgame_sdl2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        $<TARGET_FILE_DIR:cardgame_sdl2>/assets
        $<TARGET_FILE_DIR:cardgame_sdl2>/config
        $<TARGET_FILE_DIR:cardgame_sdl2>/sounds
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:cardgame_sdl2>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/config
        $<TARGET_FILE_DIR:cardgame_sdl2>/config
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/sounds
        $<TARGET_FILE_DIR:cardgame_sdl2>/sounds)
