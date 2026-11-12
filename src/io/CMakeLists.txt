femapp_add_module(io
    SOURCES
        gmsh_cursor.cpp
        gmsh_cursor.hpp
        gmsh_reader.cpp
    PUBLIC_DEPS
        fem::core)

# Private headers, also visible to the module's own unit tests.
target_include_directories(fem_io PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
