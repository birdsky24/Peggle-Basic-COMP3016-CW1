cmake
cmake_minimum_required(VERSION 3.20)
project(PeggleBasic LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_mixer REQUIRED CONFIG)

add_executable(PeggleBasic
    main.cpp
    Game.cpp
    Game.h
    Bucket.cpp
    Bucket.h
    Audio.cpp
    Audio.h
    Leaderboard.cpp
    Leaderboard.h
    Level.cpp
    Level.h
    Config.h
    Peg.h
    SDLWrappers.h
)

target_link_libraries(PeggleBasic PRIVATE SDL3::SDL3 SDL3_mixer::SDL3_mixer)