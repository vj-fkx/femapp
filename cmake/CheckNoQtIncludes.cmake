# Fails if any source file of the Qt-free modules includes a Qt header.
#
# Usage: cmake -DFEMAPP_SOURCE_DIR=<repo root> -P CheckNoQtIncludes.cmake
#
# The core, geom and mesher modules must stay usable without Qt
# (specs/constitution.md, section 4).

if(NOT FEMAPP_SOURCE_DIR)
    message(FATAL_ERROR "FEMAPP_SOURCE_DIR is not set")
endif()
get_filename_component(FEMAPP_SOURCE_DIR "${FEMAPP_SOURCE_DIR}" ABSOLUTE)

set(qt_free_modules core geom mesher)
set(qt_include_regex
    "^[ \t]*#[ \t]*include[ \t]*[<\"](Q[A-Z][A-Za-z0-9_]*[>\"/]|Qt[A-Za-z0-9]*/|q[a-z0-9_]+\\.h[>\"])")

set(violations "")
set(checked 0)

foreach(module IN LISTS qt_free_modules)
    file(GLOB_RECURSE sources
        "${FEMAPP_SOURCE_DIR}/src/${module}/*.hpp"
        "${FEMAPP_SOURCE_DIR}/src/${module}/*.h"
        "${FEMAPP_SOURCE_DIR}/src/${module}/*.cpp"
        "${FEMAPP_SOURCE_DIR}/src/${module}/*.in")

    foreach(source IN LISTS sources)
        math(EXPR checked "${checked} + 1")

        # Split into lines without letting CMake list syntax mangle the text.
        file(READ "${source}" content)
        string(REPLACE ";" " " content "${content}")
        string(REPLACE "[" "(" content "${content}")
        string(REPLACE "]" ")" content "${content}")
        string(REPLACE "\r" "" content "${content}")
        string(REPLACE "\n" ";" lines "${content}")

        set(line_number 0)
        foreach(line IN LISTS lines)
            math(EXPR line_number "${line_number} + 1")
            if(line MATCHES "${qt_include_regex}")
                file(RELATIVE_PATH relative "${FEMAPP_SOURCE_DIR}" "${source}")
                string(APPEND violations "  ${relative}:${line_number}: ${line}\n")
            endif()
        endforeach()
    endforeach()
endforeach()

if(checked EQUAL 0)
    message(FATAL_ERROR "No source files found under ${FEMAPP_SOURCE_DIR}/src")
endif()

if(violations)
    message(FATAL_ERROR
        "Qt headers are not allowed in the core, geom and mesher modules:\n${violations}")
endif()

message(STATUS "No Qt includes in ${checked} files of: ${qt_free_modules}")
