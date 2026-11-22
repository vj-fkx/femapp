# ADR 0004: parallelism, private and linked dynamically.
find_package(TBB CONFIG REQUIRED)

femapp_add_module(mesher
    SOURCES
        line_mesher.cpp
        segment_geometry.cpp
    PUBLIC_DEPS
        fem::core
        fem::geom
    PRIVATE_DEPS
        TBB::tbb)

# Private headers (detail/), also visible to the module's own unit tests.
target_include_directories(fem_mesher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
