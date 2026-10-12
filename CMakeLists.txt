# Build the native Windows game with project-local, pinned dependencies.
cmake_minimum_required(VERSION 3.24)
project(TowerMadnessReborn LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
find_package(SDL3 CONFIG REQUIRED PATHS "${CMAKE_SOURCE_DIR}/third_party/SDL3-3.4.18/cmake" NO_DEFAULT_PATH)
add_executable(TowerMadness WIN32 src/main.cpp src/game.cpp src/checks.cpp src/renderer.cpp)
target_include_directories(TowerMadness PRIVATE third_party)
target_compile_definitions(TowerMadness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(TowerMadness PRIVATE /W4 /utf-8 /permissive-)
target_link_libraries(TowerMadness PRIVATE SDL3::SDL3 opengl32 windowscodecs ole32 gdi32)
add_custom_command(TARGET TowerMadness POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:TowerMadness>)
