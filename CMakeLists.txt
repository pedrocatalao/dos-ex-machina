cmake_minimum_required(VERSION 3.20)
project(dosexmachina C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -O2")

find_package(SDL3 REQUIRED CONFIG)
set(DXM_SKYROADS "/Users/pedro/Git/skyroads-mac" CACHE PATH "skyroads-sdl checkout")

# The game core: built from the game repo with SKY_CORE=ON.  No SDL in it.
# Set the subproject's options here so a plain `cmake -S . -B build` works —
# these are DXM's business, not something the user should have to remember.
set(SKY_CORE ON CACHE BOOL "Build skyroads as a DXM core" FORCE)
set(SKY_DXM_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/src" CACHE PATH
    "Path to dxm_core.h" FORCE)
add_subdirectory(${DXM_SKYROADS} ${CMAKE_BINARY_DIR}/skyroads EXCLUDE_FROM_ALL)

add_executable(dxm
    src/main.c src/gpu.c src/chassis.c src/font.c src/dos.c src/sound.c
    src/corehost.c src/platform_dxm.c src/ui.c src/dxm_splash.c)
target_include_directories(dxm PRIVATE src ${DXM_SKYROADS}/src)
target_link_libraries(dxm PRIVATE SDL3::SDL3 skyroads_core m)
if(APPLE)
    target_link_libraries(dxm PRIVATE "-framework OpenGL")
endif()
