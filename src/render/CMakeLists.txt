find_package(Eigen3 CONFIG REQUIRED)

femapp_add_module(render
    SOURCES
        axis_gizmo_mesh.cpp
        axis_gizmo_mesh.hpp
        axis_labels.cpp
        axis_labels.hpp
        backend.cpp
        camera.cpp
        grid_spacing.cpp
        mesh_gpu.cpp
        mesh_gpu.hpp
        mesh_picker.cpp
        mesh_scene.cpp
        rhi_util.cpp
        rhi_util.hpp
        selection.cpp
        selection_overlay.cpp
        selection_overlay.hpp
        scene_renderer.cpp
        scene_renderer.hpp
        viewport.cpp
        # Listed so that AUTOMOC processes the Q_OBJECT in the public header.
        include/fem/render/viewport.hpp
    PUBLIC_DEPS
        fem::core
        fem::geom
        Eigen3::Eigen
        Qt6::Gui
        Qt6::Widgets
    PRIVATE_DEPS
        # QRhi is semi-public API (ADR 0002). It must not leak out of this module.
        Qt6::GuiPrivate)

set_target_properties(fem_render PROPERTIES AUTOMOC ON)

# Private headers, visible to the module's own unit tests only.
target_include_directories(fem_render PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# REQ-002-30: one GLSL source per shader, compiled for every backend at build
# time. GLSL 100 es / 120 are left out: the grid needs screen-space derivatives.
qt_add_shaders(fem_render "fem_render_shaders"
    PREFIX "/fem/render"
    GLSL "300es,330"
    FILES
        shaders/grid.vert
        shaders/grid.frag
        shaders/gizmo.vert
        shaders/gizmo.frag
        shaders/label.vert
        shaders/label.frag
        shaders/mesh_face.vert
        shaders/mesh_face.frag
        shaders/mesh_line.vert
        shaders/mesh_line.frag
        shaders/mesh_point.vert
        shaders/mesh_point.frag)
