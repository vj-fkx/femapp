#include <fem/setup/units.hpp>

#include <catch2/catch_test_macros.hpp>

// REQ-010-02 (REQ-001-10: the module is linked and testable).
TEST_CASE("the unit systems offered", "[setup][units]")
{
    CHECK(fem::setup::kUnitSystems.size() == 5);
    CHECK(fem::setup::kUnitSystems.front() == fem::setup::UnitSystem::NewtonMetre);
}
