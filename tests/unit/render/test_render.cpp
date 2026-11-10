#include <fem/render/module.hpp>

#include <catch2/catch_test_macros.hpp>

// REQ-001-10, REQ-001-18
TEST_CASE("render module is linked and callable", "[render]")
{
    CHECK(fem::render::moduleName() == "render");
}
