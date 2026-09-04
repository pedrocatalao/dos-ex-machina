cmake_minimum_required(VERSION 3.20)
project(dos_ex_machina VERSION 0.3.1 LANGUAGES C)
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
# Windows carries its icon as a compiled-in resource, not a file beside the
# binary.  The .ico is generated once by tools/mkico.py and committed, so no
# image tooling has to exist on the runner.
if(WIN32)
    enable_language(RC)
    set(DXM_WIN_RC packaging/windows/dxm.rc)
endif()

add_executable(dxm
    src/main.c src/gpu.c src/chassis.c src/font.c src/dos.c src/sound.c
    src/corehost.c src/coreload.c src/library.c src/ui.c src/dxm_splash.c
    src/sha256.c src/net.c src/catalog.c src/unzip.c src/install.c
    src/png.c src/art.c ${DXM_WIN_RC})
# NOTE: src/platform_dxm.c is deliberately NOT built here.  The adapter is
# compiled INTO each core (ports vendor it as src/dxm_platform.c), because a
# core loaded as a module cannot reach back into this executable for the
# plat_* symbols it needs.  The copy in src/ is the authoritative one that
# ports vendor from, exactly like src/dxm_core.h.
target_include_directories(dxm PRIVATE src)
# The version lives in project() above and nowhere else: the binary's log
# banner takes it from here, and so does the macOS bundle - Info.plist is
# generated from the .in template into the build tree, where the packaging
# step picks it up.
target_compile_definitions(dxm PRIVATE DXM_VERSION="${PROJECT_VERSION}")
configure_file(packaging/macos/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)
target_link_libraries(dxm PRIVATE SDL3::SDL3 CURL::libcurl ZLIB::ZLIB m ${CMAKE_DL_LIBS})
# GL 1.1 is exported by the system library and has to be LINKED; everything
# past it is loaded at run time (see src/glfuncs.h).  Apple has no pkg for it,
# hence the framework; everywhere else CMake finds libGL / opengl32.
# Look for libraries next to the binary first, so a release can carry the
# ones a user will not have (SDL3, mainly) instead of asking them to install
# anything.  $ORIGIN is resolved by the loader at run time, so the directory
# can be moved anywhere.
if(APPLE)
    # Two rpaths, because the binary ships in two shapes: a plain folder with
    # lib/ beside the executable, and a .app where the convention is
    # Contents/Frameworks.  Listing both means one build serves either.
    set_target_properties(dxm PROPERTIES
        BUILD_RPATH   "@executable_path/lib;@executable_path/../Frameworks"
        INSTALL_RPATH "@executable_path/lib;@executable_path/../Frameworks")
elseif(UNIX)
    set_target_properties(dxm PROPERTIES
        BUILD_RPATH   "$ORIGIN/lib"
        INSTALL_RPATH "$ORIGIN/lib")
endif()

if(APPLE)
    target_link_libraries(dxm PRIVATE "-framework OpenGL")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(dxm PRIVATE OpenGL::GL)
endif()
