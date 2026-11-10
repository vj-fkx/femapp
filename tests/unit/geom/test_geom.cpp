#include <fem/geom/kernel_check.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sstream>
#include <string>

// REQ-001-10, REQ-001-28
TEST_CASE("geometric kernel builds a box with the right volume", "[geom][kernel]")
{
    CHECK_THAT(fem::geom::boxVolume(2.0, 3.0, 4.0), Catch::Matchers::WithinRel(24.0, 1e-12));
}

// REQ-001-27
TEST_CASE("geometric kernel is version 7.8 or newer", "[geom][kernel]")
{
    const std::string version = fem::geom::kernelVersion();
    INFO("kernel version: " << version);

    std::istringstream stream(version);
    int major = 0;
    int minor = 0;
    char dot = '\0';
    stream >> major >> dot >> minor;

    REQUIRE_FALSE(stream.fail());
    REQUIRE(dot == '.');
    CHECK((major > 7 || (major == 7 && minor >= 8)));
}
