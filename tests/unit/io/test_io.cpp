#include <fem/io/module.hpp>

#include <catch2/catch_test_macros.hpp>

// REQ-001-10, REQ-001-18
TEST_CASE("io module is linked and callable", "[io]")
{
    CHECK(fem::io::moduleName() == "io");
}
