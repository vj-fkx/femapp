# ADR 0004: parallelism, private and linked dynamically.
find_package(TBB CONFIG REQUIRED)

femapp_add_module(mesher
    SOURCES
        line_mesher.cpp
    PUBLIC_DEPS
        fem::core
        fem::geom
    PRIVATE_DEPS
        TBB::tbb)
