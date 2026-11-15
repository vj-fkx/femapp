# Static libraries by default; OpenCASCADE (LGPL) must be linked dynamically
# to keep femapp distributable under the MIT license.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)

# oneTBB is meant to be a shared library (ADR 0004).
if(PORT MATCHES "^(opencascade|tbb)$")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
