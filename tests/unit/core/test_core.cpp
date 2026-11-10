#include <fem/core/version.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

// REQ-001-10, REQ-001-16
TEST_CASE("core reports the project version", "[core][version]")
{
    const fem::core::Version v = fem::core::version();
    const std::string expected =
        std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);

    CHECK(fem::core::versionString() == FEMAPP_EXPECTED_VERSION);
    CHECK(fem::core::versionString() == expected);
}
