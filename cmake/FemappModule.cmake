# femapp_add_module(<name>
#     SOURCES <files...>
#     [PUBLIC_DEPS <targets...>]
#     [PRIVATE_DEPS <targets...>])
#
# Creates the static library fem_<name> (alias fem::<name>) for the module in
# the current directory. Public headers live in include/fem/<name>/.
function(femapp_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS" ${ARGN})

    set(target fem_${name})
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(fem::${name} ALIAS ${target})

    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target}
        PUBLIC ${ARG_PUBLIC_DEPS}
        PRIVATE ${ARG_PRIVATE_DEPS})

    femapp_set_warnings(${target})
endfunction()
