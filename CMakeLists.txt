cmake_minimum_required(VERSION 3.10)
project(RoguelikeCardgame)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(cardgame
    src/main.cpp
    src/Audio.cpp
    src/Card.cpp
    src/Deck.cpp
    src/Enemy.cpp
    src/Game.cpp
    src/Run.cpp
    src/RewardPool.cpp
    src/RunStats.cpp
    src/StatusEffect.cpp
    src/Upgrade.cpp
    src/UpgradeSystem.cpp
    src/ConfigLoader.cpp
    src/UIHelper.cpp
)

# Link Windows Multimedia API for BGM playback (winmm.lib).
if(WIN32)
    target_link_libraries(cardgame winmm)
endif()

# Output the exe to the project root so it can be double-clicked directly.
set_target_properties(cardgame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}"
)
