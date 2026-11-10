#include <fem/mesher/module.hpp>

#include <catch2/catch_test_macros.hpp>

// REQ-001-10, REQ-001-18
TEST_CASE("mesher module is linked and callable", "[mesher]")
{
    CHECK(fem::mesher::moduleName() == "mesher");
}
