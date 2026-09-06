# Tests.  `ctest` from the build directory runs them all; `ctest -L golden`
# or `-L unit` picks a group.
#
# The golden test drives the real binary, so it needs a display and a GL
# 3.3 driver - a developer's machine, or a CI runner with llvmpipe.  It is
# registered only when a Python interpreter is found, since the comparison
# is a script; the unit tests need nothing beyond a C compiler.

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME golden
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/golden/run.py
                     $<TARGET_FILE:dxm> --out ${CMAKE_BINARY_DIR}/golden)
    set_tests_properties(golden PROPERTIES LABELS golden TIMEOUT 600)
else()
    message(STATUS "Python 3 not found: the golden-frame test is not registered")
endif()
