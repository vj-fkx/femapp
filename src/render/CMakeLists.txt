femapp_add_module(render
    SOURCES
        module.cpp
    PUBLIC_DEPS
        fem::core
        fem::geom
        Qt6::Gui)
