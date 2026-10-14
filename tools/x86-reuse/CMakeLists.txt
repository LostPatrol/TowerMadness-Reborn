# Build a separate Win32 host that executes the original x86 ELF code.
cmake_minimum_required(VERSION 3.20)
project(TowerMadnessX86Probe LANGUAGES CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The original ELF requires a native Windows x86 host.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Optional absolute override for the compiled host translation unit, defaulting to this
# directory's original host.cpp. build.ps1 restates -DX86_HOST_SOURCE on every configure,
# so a stale cache entry can never silently decide which source is compiled.
set(X86_HOST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/host.cpp" CACHE FILEPATH "Absolute path of the host.cpp translation unit to compile")
add_executable(TowerMadnessX86Probe "${X86_HOST_SOURCE}")
# host.cpp uses the quoted include "../../third_party/json.hpp", written relative to this
# directory. Keeping this directory on the include path makes that same include resolve to
# the original third_party/json.hpp when the compiled copy lives somewhere else.
target_include_directories(TowerMadnessX86Probe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(TowerMadnessX86Probe PRIVATE /W4 /EHsc)
target_link_libraries(TowerMadnessX86Probe PRIVATE opengl32 user32 gdi32 windowscodecs ole32)
