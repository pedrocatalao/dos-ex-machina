cmake_minimum_required(VERSION 3.20)
project(dos_ex_machina C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wno-unused-parameter -O2")

find_package(SDL3 REQUIRED CONFIG)
# libcurl for downloads (TLS, and GitHub's release URLs redirect), and zlib
# to inflate the game data archives.  Both are system libraries everywhere
# DXM runs, and curl already depends on zlib - so neither is a new burden.
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)
# DXM links no game.  A game is a .dxm module in the games directory, opened
# at run time - so the shell builds and ships on its own, and adding a game
# is a download rather than a rebuild.
add_executable(dxm
    src/main.c src/gpu.c src/chassis.c src/font.c src/dos.c src/sound.c
    src/corehost.c src/coreload.c src/library.c src/ui.c src/dxm_splash.c
    src/sha256.c src/net.c src/catalog.c src/unzip.c src/install.c)
# NOTE: src/platform_dxm.c is deliberately NOT built here.  The adapter is
# compiled INTO each core (ports vendor it as src/dxm_platform.c), because a
# core loaded as a module cannot reach back into this executable for the
# plat_* symbols it needs.  The copy in src/ is the authoritative one that
# ports vendor from, exactly like src/dxm_core.h.
target_include_directories(dxm PRIVATE src)
target_link_libraries(dxm PRIVATE SDL3::SDL3 CURL::libcurl ZLIB::ZLIB m ${CMAKE_DL_LIBS})
if(APPLE)
    target_link_libraries(dxm PRIVATE "-framework OpenGL")
endif()
