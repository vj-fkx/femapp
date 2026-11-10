cmake_minimum_required(VERSION 3.25)

# --- vcpkg setup (must happen before project()) -------------------------------
if(NOT CMAKE_TOOLCHAIN_FILE OR NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(FATAL_ERROR
        "vcpkg toolchain file not found: '${CMAKE_TOOLCHAIN_FILE}'.\n"
        "Set the VCPKG_ROOT environment variable to your vcpkg checkout and "
        "configure with a preset, e.g.: cmake --preset dev")
endif()

set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/cmake/triplets"
    CACHE STRING "Project-specific vcpkg triplets")

# Default triplet per host. OpenCASCADE (LGPL) must be a shared library, which
# the project triplets guarantee on Linux and macOS (x64-windows is all-dynamic).
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    cmake_host_system_information(RESULT _femapp_host_arch QUERY OS_PLATFORM)
    if(CMAKE_HOST_WIN32)
        set(_femapp_triplet x64-windows)
    elseif(CMAKE_HOST_APPLE AND _femapp_host_arch STREQUAL "arm64")
        set(_femapp_triplet arm64-osx-femapp)
    elseif(CMAKE_HOST_UNIX AND NOT CMAKE_HOST_APPLE AND _femapp_host_arch STREQUAL "x86_64")
        set(_femapp_triplet x64-linux-femapp)
    endif()
    if(_femapp_triplet)
        set(VCPKG_TARGET_TRIPLET "${_femapp_triplet}" CACHE STRING "vcpkg target triplet")
    else()
        message(WARNING
            "No femapp triplet for this host (${_femapp_host_arch}); using the vcpkg "
            "default. OpenCASCADE may be linked statically, which is not allowed for "
            "distributed binaries.")
    endif()
endif()

# --- Project ------------------------------------------------------------------
project(femapp
    VERSION 0.1.0
    DESCRIPTION "Finite element analysis application"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FEMAPP_BUILD_TESTS "Build the test suite" ON)
option(FEMAPP_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# All executables in one folder so they find the shared libraries at run time.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(CompilerWarnings)
include(FemappModule)

# --- Qt (installed separately, not through vcpkg) -----------------------------
set(FEMAPP_QT_MIN_VERSION 6.7)
find_package(Qt6 ${FEMAPP_QT_MIN_VERSION} QUIET COMPONENTS Core Gui Widgets LinguistTools)
if(NOT Qt6_FOUND)
    message(FATAL_ERROR
        "Qt ${FEMAPP_QT_MIN_VERSION} or newer (Core, Gui, Widgets, LinguistTools) was not found.\n"
        "Install Qt and point CMake to it with CMAKE_PREFIX_PATH, for example:\n"
        "  cmake --preset dev -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64\n"
        "or set the CMAKE_PREFIX_PATH environment variable.")
endif()

# --- Modules ------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/geom)
add_subdirectory(src/mesher)
add_subdirectory(src/io)
add_subdirectory(src/render)
add_subdirectory(src/app)

# --- Tests --------------------------------------------------------------------
if(FEMAPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
