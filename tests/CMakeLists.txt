find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

# femapp_add_test(<module>) builds tests/unit/<module>/test_<module>.cpp
# against fem::<module> and registers its test cases with CTest.
function(femapp_add_test module)
    set(target test_${module})
    add_executable(${target} "unit/${module}/test_${module}.cpp")
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
femapp_add_test(render)

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
add_test(NAME femapp_smoke COMMAND femapp --smoke-test)
set_tests_properties(femapp_smoke PROPERTIES
    TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
