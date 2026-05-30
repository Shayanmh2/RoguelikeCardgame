cmake_minimum_required(VERSION 3.10)
project(RoguelikeCardgame)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(cardgame
    src/main.cpp
    src/Card.cpp
    src/Deck.cpp
    src/Enemy.cpp
    src/Game.cpp
    src/Run.cpp
    src/RewardPool.cpp
    src/RunStats.cpp
)
