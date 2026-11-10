# Fails if a source file outside src/render includes a QRhi header.
#
# Usage: cmake -DFEMAPP_SOURCE_DIR=<repo root> -P CheckRhiConfined.cmake
#
# All use of QRhi is confined to the render module (specs/constitution.md,
# section 3, and ADR 0002). Other modules use the public interface of
# fem::render::Viewport only. QRhiWidget itself is not matched: it is the
# public base class of the viewport, not part of the QRhi API.

if(NOT FEMAPP_SOURCE_DIR)
    message(FATAL_ERROR "FEMAPP_SOURCE_DIR is not set")
endif()
get_filename_component(FEMAPP_SOURCE_DIR "${FEMAPP_SOURCE_DIR}" ABSOLUTE)

set(rhi_include_regex
    "^[ \t]*#[ \t]*include[ \t]*[<\"]([A-Za-z0-9_/]*/)?(rhi/|private/qrhi|qrhi[a-z0-9_]*[.]h|qshader[a-z0-9_]*[.]h|QRhi[>\"]|QRhi[A-VX-Z][A-Za-z]*[>\"]|QShader[A-Za-z]*[>\"])")

file(GLOB_RECURSE sources
    "${FEMAPP_SOURCE_DIR}/src/*.hpp"
    "${FEMAPP_SOURCE_DIR}/src/*.h"
    "${FEMAPP_SOURCE_DIR}/src/*.cpp")

set(violations "")
set(checked 0)

foreach(source IN LISTS sources)
    file(RELATIVE_PATH relative "${FEMAPP_SOURCE_DIR}" "${source}")
    if(relative MATCHES "^src/render/")
        continue()
    endif()
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
        if(line MATCHES "${rhi_include_regex}")
            string(APPEND violations "  ${relative}:${line_number}: ${line}\n")
        endif()
    endforeach()
endforeach()

if(checked EQUAL 0)
    message(FATAL_ERROR "No source files found under ${FEMAPP_SOURCE_DIR}/src")
endif()

if(violations)
    message(FATAL_ERROR
        "QRhi headers are only allowed in the render module:\n${violations}")
endif()

message(STATUS "No QRhi includes in ${checked} files outside src/render")
