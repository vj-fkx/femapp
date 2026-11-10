find_package(Eigen3 CONFIG REQUIRED)

femapp_add_module(render
    SOURCES
        backend.cpp
        camera.cpp
        grid_spacing.cpp
        module.cpp
    PUBLIC_DEPS
        fem::core
        fem::geom
        Eigen3::Eigen
        Qt6::Gui
        Qt6::Widgets
    PRIVATE_DEPS
        Qt6::GuiPrivate)

set_target_properties(fem_render PROPERTIES AUTOMOC ON)
