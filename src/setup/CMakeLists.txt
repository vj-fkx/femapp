# Feature 010 (ADR 0010): the model document, without Qt.
femapp_add_module(setup
    SOURCES
        actions.cpp
        build.cpp
        convert.cpp
        document.cpp
        editing.cpp
        library.cpp
        units.cpp
    PUBLIC_DEPS
        fem::core
        fem::geom
        fem::mesher)

# Private headers (detail/), also visible to the module's own unit tests.
target_include_directories(fem_setup PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
