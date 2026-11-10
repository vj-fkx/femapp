find_package(Eigen3 CONFIG REQUIRED)
find_package(OpenCASCADE CONFIG REQUIRED)

# OpenCASCADE is a PRIVATE dependency: its headers must never leak out of this
# module, and it must be a shared library (LGPL).
set(FEMAPP_OCCT_LIBS TKernel TKMath TKG3d TKGeomBase TKBRep TKTopAlgo TKPrim)

foreach(lib IN LISTS FEMAPP_OCCT_LIBS)
    get_target_property(_type ${lib} TYPE)
    if(NOT _type STREQUAL "SHARED_LIBRARY")
        message(FATAL_ERROR
            "OpenCASCADE library ${lib} is a ${_type}; it must be a shared library. "
            "Use one of the project vcpkg triplets (see cmake/triplets).")
    endif()
endforeach()

femapp_add_module(geom
    SOURCES
        kernel_check.cpp
    PUBLIC_DEPS
        Eigen3::Eigen
    PRIVATE_DEPS
        ${FEMAPP_OCCT_LIBS})

target_include_directories(fem_geom SYSTEM PRIVATE "${OpenCASCADE_INCLUDE_DIR}")
