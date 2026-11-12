find_package(Eigen3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

configure_file(version_config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version_config.hpp" @ONLY)

femapp_add_module(core
    SOURCES
        element_reference_coordinates.inc
        element_type.cpp
        id_ranges.cpp
        mesh.cpp
        mesh_builder.cpp
        mesh_measure.cpp
        mesh_surface.cpp
        version.cpp
    PUBLIC_DEPS
        Eigen3::Eigen
        spdlog::spdlog
        tl::expected)

target_include_directories(fem_core PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    # Private headers, also visible to the module's own unit tests.
    "${CMAKE_CURRENT_SOURCE_DIR}")
