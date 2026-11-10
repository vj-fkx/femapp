find_package(Eigen3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

configure_file(version_config.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version_config.hpp" @ONLY)

femapp_add_module(core
    SOURCES
        version.cpp
    PUBLIC_DEPS
        Eigen3::Eigen
        spdlog::spdlog)

target_include_directories(fem_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
