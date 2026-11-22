#include <fem/mesher/line_mesher.hpp>

#include <catch2/catch_test_macros.hpp>

// REQ-009-09 (REQ-001-10: the module is linked and callable).
TEST_CASE("the group of an entity is named by its id", "[mesher][line]")
{
    CHECK(fem::mesher::entityGroupName(fem::geom::EntityId{42}) == "entity 42");
    CHECK(fem::mesher::entityGroupName(fem::geom::EntityId{1}) == "entity 1");
}

// REQ-009-14: an empty drawing has no lines.
TEST_CASE("an empty drawing cannot be meshed", "[mesher][line]")
{
    const fem::geom::Drawing drawing;
    const auto result = fem::mesher::meshLines(drawing);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == fem::mesher::LineMeshError::Kind::NoLines);
}
