cmake_minimum_required(VERSION 3.20)
project(dos_ex_machina VERSION 0.4.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
# Optimisation belongs to the build type, and a plain `cmake -S . -B build`
# should not hand out an unoptimised chassis render by accident.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(DXM_WERROR "Treat compiler warnings as errors (CI turns this on)" OFF)

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
    src/main.c src/gpu.c src/font.c src/sound.c
    src/dos/dos.c src/dos/term.c src/dos/shell.c src/dos/boot.c src/dos/nc.c
    src/chassis/chassis.c src/chassis/canvas.c src/chassis/surface.c src/chassis/bezel.c
    src/chassis/parts.c src/chassis/marks.c src/chassis/knobs.c src/chassis/layout.c
    src/corehost.c src/coreload.c src/library.c src/ui.c src/gen/splash.c
    src/sha256.c src/net.c src/catalog.c src/unzip.c src/install.c
    src/png.c src/art.c src/disk.c ${DXM_WIN_RC})
# contract/ is what a port vendors: dxm_core.h, the interface both sides
# compile against, and dxm_platform.c, the shared adapter.  The adapter is
# deliberately NOT built here - it is compiled INTO each core, because a
# module cannot reach back into this executable for the plat_* symbols it
# needs.  The host only includes the header.
target_include_directories(dxm PRIVATE src contract ${CMAKE_BINARY_DIR}/generated)
# The shaders are GLSL files under shaders/, baked into one header at build
# time so the binary stays self-contained.  Editing a shader rebuilds gpu.c.
set(DXM_SHADERS
    shaders/quad.vert shaders/persist.frag shaders/blur.frag shaders/composite.frag
    shaders/burn.frag shaders/splash.frag shaders/fade.frag shaders/overlay.frag)
list(TRANSFORM DXM_SHADERS PREPEND ${CMAKE_SOURCE_DIR}/)
# The list crosses a command line, where ';' would split it into separate
# arguments; it travels comma-joined and the script splits it again.
string(REPLACE ";" "," DXM_SHADERS_ARG "${DXM_SHADERS}")
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/shaders.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND ${CMAKE_COMMAND} -DOUT=${CMAKE_BINARY_DIR}/generated/shaders.h
            -DFILES=${DXM_SHADERS_ARG} -P ${CMAKE_SOURCE_DIR}/tools/embed.cmake
    DEPENDS ${DXM_SHADERS} ${CMAKE_SOURCE_DIR}/tools/embed.cmake
    COMMENT "Baking shaders/ into shaders.h"
    VERBATIM)
target_sources(dxm PRIVATE ${CMAKE_BINARY_DIR}/generated/shaders.h)
# The warning set the code is kept clean under, on both compilers it ships
# with.  -Wswitch-enum is deliberately absent: a switch over SDL's scancodes
# with a default is the right shape, and that flag objects to it.
target_compile_options(dxm PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wvla
    -Wdouble-promotion -Wcast-qual -Wimplicit-fallthrough -Wundef -Wformat=2
    # GCC's truncation analysis fires on every bounded path build
    # ("%sgames" into a 1024-byte buffer might not fit); the bound is the
    # point of snprintf, so that one is off.  The rest of -Wall stays.
    $<$<C_COMPILER_ID:GNU>:-Wno-format-truncation>
    $<$<BOOL:${DXM_WERROR}>:-Werror>)
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

# Tests are on by default and cost nothing to configure; a packaging build
# that wants none passes -DDXM_TESTS=OFF.
option(DXM_TESTS "Register the test suite with CTest" ON)
if(DXM_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

