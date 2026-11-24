#include "detail/segment_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <string>

// Feature 009, T02 (design §5.3): the closest points and the contacts of
// two pieces, against the values worked out by hand (1e-12 relative), and
// the same classification with the drawing moved far from the origin and
// scaled (NFR-009-02).

namespace {

// The value of an optional the test requires: REQUIRE, then value_or, so
// that static analysis sees the check (clang-tidy 19 on Linux).
template <typename T>
T required(const std::optional<T>& value)
{
    REQUIRE(value.has_value());
    return value.value_or(T{});
}

using fem::mesher::detail::classify;
using fem::mesher::detail::closestPoints;
using fem::mesher::detail::ContactKind;
using fem::mesher::detail::interiorParameter;
using fem::mesher::detail::Vec3;

constexpr double kTol = 1e-9; // a drawing of diagonal 1 or less

bool near(double a, double b, double scale = 1.0)
{
    return std::abs(a - b) <= 1e-12 * std::max(1.0, scale);
}

bool near(const Vec3& a, const Vec3& b, double scale = 1.0)
{
    return (a - b).norm() <= 1e-12 * std::max(1.0, scale);
}

} // namespace

TEST_CASE("closest points of two segments", "[mesher][segment]")
{
    SECTION("crossing in the plane")
    {
        const auto c = closestPoints({0, 0, 0}, {2, 0, 0}, {1, -1, 0}, {1, 3, 0});
        CHECK(near(c.s, 0.5));
        CHECK(near(c.t, 0.25));
        CHECK(near(c.first, Vec3(1, 0, 0)));
        CHECK(near(c.second, Vec3(1, 0, 0)));
        CHECK(near(c.distance, 0.0));
    }
    SECTION("skew lines in 3D")
    {
        // x axis and the line through (1, -1, 2) along y: closest at (1, 0, 0)
        // and (1, 0, 2).
        const auto c = closestPoints({0, 0, 0}, {4, 0, 0}, {1, -1, 2}, {1, 1, 2});
        CHECK(near(c.s, 0.25));
        CHECK(near(c.t, 0.5));
        CHECK(near(c.distance, 2.0));
    }
    SECTION("clamped to the ends")
    {
        const auto c = closestPoints({0, 0, 0}, {1, 0, 0}, {3, 1, 0}, {3, 5, 0});
        CHECK(near(c.s, 1.0));
        CHECK(near(c.t, 0.0));
        CHECK(near(c.distance, std::sqrt(5.0)));
    }
    SECTION("parallel")
    {
        const auto c = closestPoints({0, 0, 0}, {2, 0, 0}, {1, 1, 0}, {3, 1, 0});
        CHECK(near(c.distance, 1.0));
    }
}

TEST_CASE("a point on the interior of a segment", "[mesher][segment]")
{
    const Vec3 a(0, 0, 0);
    const Vec3 b(4, 0, 0);
    const auto on = interiorParameter({1, 0, 0}, a, b, kTol);
    REQUIRE(on.has_value());
    CHECK(near(required(on), 0.25));
    CHECK(interiorParameter({1, 0.5e-9, 0}, a, b, kTol).has_value());
    CHECK_FALSE(interiorParameter({1, 2e-9, 0}, a, b, kTol).has_value());
    // Within the tolerance of an end: an end, not the interior.
    CHECK_FALSE(interiorParameter({0.5e-9, 0, 0}, a, b, kTol).has_value());
    CHECK_FALSE(interiorParameter({4.0 - 0.5e-9, 0, 0}, a, b, kTol).has_value());
    CHECK_FALSE(interiorParameter({5, 0, 0}, a, b, kTol).has_value());
}

TEST_CASE("the contact of two pieces", "[mesher][segment]")
{
    SECTION("crossing")
    {
        const auto c = classify({0, 0, 0}, {2, 0, 0}, {1, -1, 0}, {1, 3, 0}, kTol);
        CHECK(c.kind == ContactKind::Crossing);
        REQUIRE(c.splitFirst.has_value());
        REQUIRE(c.splitSecond.has_value());
        CHECK(near(required(c.splitFirst), 0.5));
        CHECK(near(required(c.splitSecond), 0.25));
        CHECK(near(c.position, Vec3(1, 0, 0)));
    }
    SECTION("crossing in 3D")
    {
        const auto c = classify({0, 0, 0}, {2, 2, 2}, {2, 0, 0}, {0, 2, 2}, kTol);
        CHECK(c.kind == ContactKind::Crossing);
        CHECK(near(c.position, Vec3(1, 1, 1)));
    }
    SECTION("the end of the second on the first: a T joint")
    {
        const auto c = classify({0, 0, 0}, {4, 0, 0}, {1, 0, 0}, {1, 3, 0}, kTol);
        CHECK(c.kind == ContactKind::TJoint);
        REQUIRE(c.splitFirst.has_value());
        CHECK_FALSE(c.splitSecond.has_value());
        CHECK(near(required(c.splitFirst), 0.25));
        CHECK(near(c.position, Vec3(1, 0, 0)));
    }
    SECTION("the end of the first on the second")
    {
        const auto c = classify({1, 3, 0}, {1, 0, 0}, {0, 0, 0}, {4, 0, 0}, kTol);
        CHECK(c.kind == ContactKind::TJoint);
        CHECK_FALSE(c.splitFirst.has_value());
        REQUIRE(c.splitSecond.has_value());
        CHECK(near(required(c.splitSecond), 0.25));
        CHECK(near(c.position, Vec3(1, 0, 0)));
    }
    SECTION("nearly a T: within the tolerance, and beyond it")
    {
        CHECK(classify({0, 0, 0}, {4, 0, 0}, {1, 0.5e-9, 0}, {1, 3, 0}, kTol).kind ==
              ContactKind::TJoint);
        CHECK(classify({0, 0, 0}, {4, 0, 0}, {1, 2e-9, 0}, {1, 3, 0}, kTol).kind ==
              ContactKind::None);
    }
    SECTION("end to end")
    {
        CHECK(classify({0, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 1, 0}, kTol).kind == ContactKind::Ends);
        CHECK(classify({0, 0, 0}, {1, 0, 0}, {1, 0, 0}, {2, 0, 0}, kTol).kind == ContactKind::Ends);
    }
    SECTION("skew lines do not touch")
    {
        CHECK(classify({0, 0, 0}, {4, 0, 0}, {1, -1, 2}, {1, 1, 2}, kTol).kind ==
              ContactKind::None);
    }
    SECTION("parallel apart")
    {
        CHECK(classify({0, 0, 0}, {2, 0, 0}, {1, 1, 0}, {3, 1, 0}, kTol).kind == ContactKind::None);
    }
    SECTION("collinear with a shared stretch: an overlap")
    {
        const auto c = classify({0, 0, 0}, {3, 0, 0}, {2, 0, 0}, {5, 0, 0}, kTol);
        CHECK(c.kind == ContactKind::Overlap);
        CHECK(near(c.from, Vec3(2, 0, 0)));
        CHECK(near(c.to, Vec3(3, 0, 0)));
        // Reversed, and one inside the other.
        const auto inside = classify({0, 0, 0}, {4, 0, 0}, {3, 0, 0}, {1, 0, 0}, kTol);
        CHECK(inside.kind == ContactKind::Overlap);
        CHECK(near(inside.from, Vec3(1, 0, 0)));
        CHECK(near(inside.to, Vec3(3, 0, 0)));
    }
    SECTION("collinear apart")
    {
        CHECK(classify({0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, kTol).kind == ContactKind::None);
    }
}

// NFR-009-02: the same contacts with the pieces moved far from the origin
// and scaled; the tolerance follows the scale (1e-9 of a diagonal >= 1).
TEST_CASE("contacts do not depend on the place or the scale", "[mesher][segment]")
{
    struct Case
    {
        std::array<Vec3, 4> points{};
        ContactKind kind = ContactKind::None;
    };
    const std::array<Case, 7> cases{{
        {.points = {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(1, -1, 0), Vec3(1, 3, 0)},
         .kind = ContactKind::Crossing},
        {.points = {Vec3(0, 0, 0), Vec3(2, 2, 2), Vec3(2, 0, 0), Vec3(0, 2, 2)},
         .kind = ContactKind::Crossing},
        {.points = {Vec3(0, 0, 0), Vec3(4, 0, 0), Vec3(1, 0, 0), Vec3(1, 3, 0)},
         .kind = ContactKind::TJoint},
        {.points = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0)},
         .kind = ContactKind::Ends},
        {.points = {Vec3(0, 0, 0), Vec3(4, 0, 0), Vec3(1, -1, 2), Vec3(1, 1, 2)},
         .kind = ContactKind::None},
        {.points = {Vec3(0, 0, 0), Vec3(3, 0, 0), Vec3(2, 0, 0), Vec3(5, 0, 0)},
         .kind = ContactKind::Overlap},
        {.points = {Vec3(0.3, 0.1, 0.7), Vec3(2.9, 1.7, 0.2), Vec3(1.6, 0.9, 0.45),
                    Vec3(1.6, 4, 5)},
         .kind = ContactKind::TJoint},
    }};
    const Vec3 far(1e6, -1e6, 1e6);
    for (const double scale : {1.0, 1e3, 1e6}) {
        for (const Vec3& shift : {Vec3(Vec3::Zero()), far}) {
            for (std::size_t i = 0; i < cases.size(); ++i) {
                const auto& p = cases.at(i).points;
                INFO("case " << i << ", scale " << scale << ", shift " << shift.x());
                const auto at = [&](std::size_t k) { return Vec3(scale * p.at(k) + shift); };
                // The diagonal of these drawings is >= 1 for any scale here.
                const double tol = 1e-9 * scale * 5.0;
                const auto c = classify(at(0), at(1), at(2), at(3), tol);
                CHECK(c.kind == cases.at(i).kind);
            }
        }
    }
}
