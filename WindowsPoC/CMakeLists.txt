cmake_minimum_required(VERSION 3.24)
project(CompositorWindowsPoC LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CompositorWindowsPoC WIN32
    src/main.cpp
    ../Compositor/Rendering/LevelsPixels.c
)

target_include_directories(CompositorWindowsPoC PRIVATE
    ../Compositor/Rendering
)

target_compile_definitions(CompositorWindowsPoC PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(CompositorWindowsPoC PRIVATE
    windowscodecs
    d2d1
    ole32
)
