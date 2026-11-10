qt_add_executable(femapp WIN32 MACOSX_BUNDLE
    main.cpp
    cli_options.cpp
    cli_options.hpp
    main_window.cpp
    main_window.hpp)

set_target_properties(femapp PROPERTIES AUTOMOC ON)
target_compile_features(femapp PRIVATE cxx_std_20)
target_link_libraries(femapp PRIVATE
    fem::core
    fem::geom
    fem::mesher
    fem::io
    fem::render
    Qt6::Widgets)
femapp_set_warnings(femapp)

# REQ-001-17: translations are embedded as resources under :/i18n.
qt_add_translations(femapp
    TS_FILES i18n/femapp_pt_BR.ts
    RESOURCE_PREFIX "/i18n")

# REQ-001-30: on Windows, copy the Qt DLLs and plugins next to the executables
# so that femapp and the tests run straight from the build tree.
if(WIN32)
    get_target_property(_qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_location}" DIRECTORY)
    add_custom_command(TARGET femapp POST_BUILD
        COMMAND "${_qt_bin_dir}/windeployqt.exe"
                --no-translations --no-compiler-runtime
                --no-system-d3d-compiler --no-opengl-sw
                # The smoke test runs with QT_QPA_PLATFORM=offscreen.
                --include-plugins qoffscreen
                "$<TARGET_FILE:femapp>"
        COMMENT "Deploying Qt runtime next to femapp"
        VERBATIM)
endif()
