#include <fem/core/expected.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

fem::core::Expected<int> parsePositive(int value)
{
    if (value <= 0) {
        return fem::core::makeError("The value must be positive.");
    }
    return value;
}

} // namespace

// Base for REQ-003-09 and REQ-003-22: expected failures travel as values.
TEST_CASE("Expected carries either a value or an error", "[core][expected]")
{
    const fem::core::Expected<int> good = parsePositive(3);
    REQUIRE(good.has_value());
    CHECK(*good == 3);

    const fem::core::Expected<int> bad = parsePositive(-1);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message == "The value must be positive.");

    const fem::core::Expected<void> nothing{};
    CHECK(nothing.has_value());
}
