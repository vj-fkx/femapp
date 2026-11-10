find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# femapp_add_test(<module> [SOURCES <files...>]) builds the test executable
# test_<module> against fem::<module> and registers its test cases with CTest.
# Without SOURCES it builds tests/unit/<module>/test_<module>.cpp; file names
# in SOURCES are relative to tests/unit/<module>/.
function(femapp_add_test module)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCES)
        set(ARG_SOURCES "test_${module}.cpp")
    endif()
    list(TRANSFORM ARG_SOURCES PREPEND "unit/${module}/")

    set(target test_${module})
    add_executable(${target} ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE fem::${module} Catch2::Catch2WithMain)
    femapp_set_warnings(${target})
    # PRE_TEST: discover at ctest time, when the shared libraries are in place.
    catch_discover_tests(${target} DISCOVERY_MODE PRE_TEST)
endfunction()

femapp_add_test(core)
target_compile_definitions(test_core PRIVATE FEMAPP_EXPECTED_VERSION="${PROJECT_VERSION}")

femapp_add_test(geom)
femapp_add_test(mesher)
femapp_add_test(io)
femapp_add_test(render SOURCES
    test_axis_gizmo_mesh.cpp
    test_backend.cpp
    test_camera.cpp
    test_grid_spacing.cpp
    test_scene_renderer.cpp)
# The renderer tests reach into the module's private headers and create a QRhi
# with the Null backend, which needs no GPU and no window.
target_include_directories(test_render PRIVATE "${PROJECT_SOURCE_DIR}/src/render")
target_link_libraries(test_render PRIVATE Qt6::GuiPrivate)

# REQ-001-09: the Qt-free modules must not include Qt headers.
add_test(NAME check_no_qt_includes
    COMMAND "${CMAKE_COMMAND}" "-DFEMAPP_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckNoQtIncludes.cmake")

# REQ-001-14, REQ-001-16: --version prints the project version without a window.
string(REPLACE "." "\\." _femapp_version_regex "${PROJECT_VERSION}")
add_test(NAME femapp_version COMMAND femapp --version)
set_tests_properties(femapp_version PROPERTIES
    PASS_REGULAR_EXPRESSION "femapp ${_femapp_version_regex}")

# REQ-001-11, REQ-001-15: the main window opens and the app exits on its own.
# REQ-002-29: the viewport renders at least one frame, here with the Null backend.
add_test(NAME femapp_smoke COMMAND femapp --smoke-test)
set_tests_properties(femapp_smoke PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FEM_RHI_BACKEND=null"
    PASS_REGULAR_EXPRESSION "smoke test: frame rendered")

# REQ-002-06: with no graphics backend the window still opens and reports it.
add_test(NAME femapp_smoke_no_gpu COMMAND femapp --smoke-test)
set_tests_properties(femapp_smoke_no_gpu PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FEM_RHI_BACKEND=fail"
    PASS_REGULAR_EXPRESSION "smoke test: viewport unavailable")

# REQ-002-10: QRhi must stay inside the render module.
add_test(NAME check_rhi_confined
    COMMAND "${CMAKE_COMMAND}" "-DFEMAPP_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CheckRhiConfined.cmake")
