# Tests.  `ctest` from the build directory runs them all; `ctest -L unit`
# or `-L golden` picks a group.
#
# The unit tests build each module from its own sources with exactly the
# libraries that module needs, which is also a statement of what it
# depends on.  They need no display.  The golden test drives the real
# binary, so it needs a display and a GL 3.3 driver - a developer's
# machine, or a CI runner with llvmpipe.

set(UNIT_TMP ${CMAKE_BINARY_DIR}/unit-tmp)
file(MAKE_DIRECTORY ${UNIT_TMP})

# unit_test(<name> SOURCES <src...> [LIBS <lib...>])
function(unit_test name)
    cmake_parse_arguments(T "" "" "SOURCES;LIBS" ${ARGN})
    add_executable(test_${name} unit/test_${name}.c ${T_SOURCES})
    target_include_directories(test_${name} PRIVATE ${CMAKE_SOURCE_DIR}/src
                               ${CMAKE_SOURCE_DIR}/contract ${CMAKE_CURRENT_SOURCE_DIR}/unit)
    target_compile_options(test_${name} PRIVATE ${DXM_WARNINGS} ${DXM_SANITIZE_FLAGS})
    target_link_options(test_${name} PRIVATE ${DXM_SANITIZE_FLAGS})
    target_compile_definitions(test_${name} PRIVATE
        TEST_TMP="${UNIT_TMP}"
        TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/unit/fixtures"
        TEST_ASSETS="${CMAKE_SOURCE_DIR}/assets"
        TEST_REPO="${CMAKE_SOURCE_DIR}")
    if(T_LIBS)
        target_link_libraries(test_${name} PRIVATE ${T_LIBS})
    endif()
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES LABELS unit TIMEOUT 60)
endfunction()

set(SRC ${CMAKE_SOURCE_DIR}/src)
unit_test(sha256  SOURCES ${SRC}/sha256.c)
unit_test(font    SOURCES ${SRC}/font.c)
unit_test(unzip   SOURCES ${SRC}/unzip.c ${SRC}/sha256.c LIBS ZLIB::ZLIB)
unit_test(png     SOURCES ${SRC}/png.c ${SRC}/sha256.c   LIBS ZLIB::ZLIB)
unit_test(disk    SOURCES ${SRC}/disk.c                  LIBS SDL3::SDL3)
unit_test(catalogue SOURCES ${SRC}/catalogue.c ${SRC}/net.c ${SRC}/library.c
                          ${SRC}/coreload.c ${SRC}/unzip.c
                  LIBS SDL3::SDL3 CURL::libcurl ZLIB::ZLIB ${CMAKE_DL_LIBS})

# A real loadable module exercising PORTING 3.1/3.2. This is intentionally
# not just a compile check: Windows must survive the non-local unwind, unload
# the module, reopen it, and run the same path again.
add_library(test_unwind_module MODULE unit/unwind_module.c)
target_include_directories(test_unwind_module PRIVATE ${CMAKE_SOURCE_DIR}/contract)
target_compile_options(test_unwind_module PRIVATE ${DXM_WARNINGS})
set_target_properties(test_unwind_module PROPERTIES
    PREFIX ""
    SUFFIX ".dxm"
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

add_executable(test_unwind unit/test_unwind.c ${SRC}/coreload.c)
target_include_directories(test_unwind PRIVATE ${CMAKE_SOURCE_DIR}/src
                           ${CMAKE_SOURCE_DIR}/contract ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_compile_options(test_unwind PRIVATE ${DXM_WARNINGS})
target_link_libraries(test_unwind PRIVATE ${CMAKE_DL_LIBS})
add_test(NAME unwind COMMAND test_unwind $<TARGET_FILE:test_unwind_module>)
set_tests_properties(unwind PROPERTIES LABELS unit TIMEOUT 60)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME golden
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/golden/run.py
                     $<TARGET_FILE:dxm> --out ${CMAKE_BINARY_DIR}/golden)
    set_tests_properties(golden PROPERTIES LABELS golden TIMEOUT 600)
else()
    message(STATUS "Python 3 not found: the golden-frame test is not registered")
endif()
