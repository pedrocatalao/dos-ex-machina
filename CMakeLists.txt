cmake_minimum_required(VERSION 3.20)
project(dos_ex_machina VERSION 2.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)
# Optimisation belongs to the build type, and a plain `cmake -S . -B build`
# should not hand out an unoptimised chassis render by accident.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
option(DXM_WERROR "Treat compiler warnings as errors (CI turns this on)" OFF)
# AddressSanitizer and UndefinedBehaviorSanitizer, for a Debug build that
# runs the tests: undefined behaviour aborts instead of being recovered
# from, which is what makes a signed overflow a failure rather than a
# silently wrong loop bound.
option(DXM_SANITIZE "Build with ASan and UBSan" OFF)
if(DXM_SANITIZE)
    set(DXM_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined
                           -fno-omit-frame-pointer)
endif()

find_package(SDL3 REQUIRED CONFIG)
# DXM links no emulator.  DOSBox Pure is a libretro core opened at run time
# from beside the program (src/dosbox/); it is built below, from its own
# tree and with its own Makefile, into the same directory as dxm.
# Windows carries its icon as a compiled-in resource, not a file beside the
# binary.  The .ico is generated once by tools/mkico.py and committed, so no
# image tooling has to exist on the runner.
if(WIN32)
    enable_language(RC)
    set(DXM_WIN_RC packaging/windows/dxm.rc)
endif()

add_executable(dxm
    src/main.c src/app.c src/log.c src/splash.c src/theatre.c src/input.c src/cursor.c
    src/gpu.c src/font.c src/sound.c src/ui.c src/gen/splash.c
    src/dos/dos.c src/dos/term.c src/dos/boot.c
    src/chassis/chassis.c src/chassis/canvas.c src/chassis/surface.c src/chassis/bezel.c
    src/chassis/parts.c src/chassis/marks.c src/chassis/knobs.c src/chassis/layout.c
    src/dosbox/dosbox.c src/dosbox/keys.c src/dosbox/motd.c
    ${DXM_WIN_RC})
target_include_directories(dxm PRIVATE src ${CMAKE_BINARY_DIR}/generated)
# The shaders are GLSL files under shaders/, baked into one header at build
# time so the binary stays self-contained.  Editing a shader rebuilds gpu.c.
set(DXM_SHADERS
    shaders/quad.vert shaders/persist.frag shaders/blur.frag shaders/composite.frag
    shaders/burn.frag shaders/splash.frag shaders/fade.frag shaders/overlay.frag
    shaders/edge.frag shaders/ease.frag shaders/glow.frag)
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
set(DXM_WARNINGS
    -Wall -Wextra -Wno-unused-parameter
    -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wvla
    -Wdouble-promotion -Wcast-qual -Wimplicit-fallthrough -Wundef -Wformat=2
    # GCC's truncation analysis fires on every bounded path build
    # ("%sgames" into a 1024-byte buffer might not fit); the bound is the
    # point of snprintf, so that one is off.  The rest of -Wall stays.
    $<$<C_COMPILER_ID:GNU>:-Wno-format-truncation>
    $<$<BOOL:${DXM_WERROR}>:-Werror>)
target_compile_options(dxm PRIVATE ${DXM_WARNINGS} ${DXM_SANITIZE_FLAGS})
target_link_options(dxm PRIVATE ${DXM_SANITIZE_FLAGS})
# The version lives in project() above and nowhere else: the binary's log
# banner takes it from here, and so does the macOS bundle - Info.plist is
# generated from the .in template into the build tree, where the packaging
# step picks it up.
target_compile_definitions(dxm PRIVATE DXM_VERSION="${PROJECT_VERSION}")
configure_file(packaging/macos/Info.plist.in ${CMAKE_BINARY_DIR}/Info.plist @ONLY)
target_link_libraries(dxm PRIVATE SDL3::SDL3 m ${CMAKE_DL_LIBS})
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

# The DOS: DOSBox Pure, from the fork pinned as a submodule in
# external/dosbox-pure, built with its own Makefile and copied beside dxm,
# where the machine looks for it.  The objects stay in the submodule's own
# (ignored) build/ directory, one per architecture, so a universal macOS
# build compiles it once for each and joins them with lipo.  The compiler is
# the C++ one CMake finds, so the core is built by the same toolchain as the
# machine.  -DDXM_CORE=OFF skips all of it, for a machine run with
# --dosbox-core.
option(DXM_CORE "Build the DOSBox Pure core from external/dosbox-pure" ON)
if(DXM_CORE)
    set(DXM_CORE_SRC ${CMAKE_SOURCE_DIR}/external/dosbox-pure)
    if(NOT EXISTS ${DXM_CORE_SRC}/Makefile)
        message(FATAL_ERROR "external/dosbox-pure is empty - run: git submodule update --init")
    endif()
    enable_language(CXX)
    find_program(DXM_MAKE NAMES gmake make mingw32-make REQUIRED)
    cmake_host_system_information(RESULT DXM_JOBS QUERY NUMBER_OF_LOGICAL_CORES)
    set(DXM_CORE_CXX ${CMAKE_CXX_COMPILER})
    set(DXM_CORE_ARGS "")
    if(WIN32)
        set(DXM_CORE_NAME dosbox_pure_libretro.dll)
        # The Makefile's own Windows test looks for a drive letter in the
        # path, which MSYS2's make does not give it.  -static puts the C++
        # runtime inside the DLL, so it needs nothing beside it.
        list(APPEND DXM_CORE_ARGS platform=windows)
        set(DXM_CORE_CXX "${CMAKE_CXX_COMPILER} -static")
    elseif(APPLE)
        set(DXM_CORE_NAME dosbox_pure_libretro.dylib)
    else()
        set(DXM_CORE_NAME dosbox_pure_libretro.so)
    endif()
    set(DXM_CORE_ARCHS native)
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        set(DXM_CORE_ARCHS ${CMAKE_OSX_ARCHITECTURES})
    endif()
    file(GLOB_RECURSE DXM_CORE_DEPS CONFIGURE_DEPENDS
         ${DXM_CORE_SRC}/*.cpp ${DXM_CORE_SRC}/*.h ${DXM_CORE_SRC}/*.c)
    list(APPEND DXM_CORE_DEPS ${DXM_CORE_SRC}/Makefile)
    set(DXM_CORE_SLICES "")
    set(DXM_CORE_COMMANDS "")
    foreach(arch IN LISTS DXM_CORE_ARCHS)
        set(flags "")
        if(NOT arch STREQUAL "native")
            set(flags "-arch ${arch}")
        endif()
        if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
            string(APPEND flags " -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        endif()
        # OUTNAME and BUILDDIR are relative to the submodule: a Windows
        # absolute path carries a drive letter, whose colon make reads as a
        # rule separator.
        set(slice build/dxm-${arch}/${DXM_CORE_NAME})
        list(APPEND DXM_CORE_SLICES ${DXM_CORE_SRC}/${slice})
        list(APPEND DXM_CORE_COMMANDS
             COMMAND ${DXM_MAKE} -C ${DXM_CORE_SRC} -j${DXM_JOBS} BUILDDIR=dxm-${arch}
                     OUTNAME=${slice} "CXX=${DXM_CORE_CXX}" "ARCHFLAGS=${flags}" ${DXM_CORE_ARGS})
    endforeach()
    list(LENGTH DXM_CORE_SLICES nslices)
    if(nslices GREATER 1)
        set(DXM_CORE_JOIN COMMAND lipo -create -output ${CMAKE_BINARY_DIR}/${DXM_CORE_NAME}
                          ${DXM_CORE_SLICES})
    else()
        set(DXM_CORE_JOIN COMMAND ${CMAKE_COMMAND} -E copy ${DXM_CORE_SLICES}
                          ${CMAKE_BINARY_DIR}/${DXM_CORE_NAME})
    endif()
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/${DXM_CORE_NAME}
        ${DXM_CORE_COMMANDS}
        ${DXM_CORE_JOIN}
        DEPENDS ${DXM_CORE_DEPS}
        COMMENT "Building DOSBox Pure (${DXM_CORE_ARCHS})"
        VERBATIM)
    add_custom_target(dosbox_core ALL DEPENDS ${CMAKE_BINARY_DIR}/${DXM_CORE_NAME})
endif()

# Tests are on by default and cost nothing to configure; a packaging build
# that wants none passes -DDXM_TESTS=OFF.
option(DXM_TESTS "Register the test suite with CTest" ON)
if(DXM_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

