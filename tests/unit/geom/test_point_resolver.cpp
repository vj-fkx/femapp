#include <fem/geom/point_resolver.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

using fem::geom::DraftingSettings;
using fem::geom::Drawing;
using fem::geom::Entity;
using fem::geom::LineEntity;
using fem::geom::PointResolver;
using fem::geom::ResolvedPoint;
using fem::geom::ScreenView;
using fem::geom::SnapMode;
using fem::geom::Vec3;

namespace {

constexpr double kWidth = 800.0;
constexpr double kHeight = 600.0;
constexpr double kHalfWidth = 10.0; // world units across half the screen
constexpr double kHalfHeight = 7.5; // 40 pixels per unit
constexpr double kTolerance = 1e-9;

// Orthographic view from above, centred on the origin, with the cursor over
// the world point (x, y).
ScreenView topView(double x, double y)
{
    ScreenView view;
    view.viewProjection << 1.0 / kHalfWidth, 0, 0, 0, //
        0, 1.0 / kHalfHeight, 0, 0,                   //
        0, 0, -0.01, 0,                               //
        0, 0, 0, 1;
    view.width = kWidth;
    view.height = kHeight;
    view.orthographic = true;
    view.rayOrigin = Vec3(x, y, 100.0);
    view.rayDirection = -Vec3::UnitZ();
    view.cursor = {(x / kHalfWidth + 1.0) * 0.5 * kWidth, (1.0 - y / kHalfHeight) * 0.5 * kHeight};
    return view;
}

void addLines(Drawing& drawing, const std::vector<std::pair<Vec3, Vec3>>& lines)
{
    fem::geom::DrawingEdit edit;
    for (const auto& [a, b] : lines) {
        edit.added.push_back({.id = drawing.nextId(), .shape = LineEntity{.start = a, .end = b}});
    }
    drawing.apply(edit);
}

ResolvedPoint resolved(const Drawing& drawing, const ScreenView& view,
                       const DraftingSettings& settings = {},
                       const std::optional<Vec3>& last = std::nullopt)
{
    const PointResolver resolver;
    const auto point = resolver.resolve(drawing, view, settings, last);
    REQUIRE(point.has_value());
    return point.value_or(ResolvedPoint{});
}

bool near(const Vec3& a, const Vec3& b)
{
    return (a - b).norm() < kTolerance;
}

} // namespace

// REQ-005-19: endpoint and midpoint within the aperture, nothing outside.
TEST_CASE("snaps to endpoints and midpoints within the aperture", "[geom][snap]")
{
    Drawing drawing;
    addLines(drawing, {{Vec3(0, 0, 0), Vec3(5, 0, 0)}});

    const auto end = resolved(drawing, topView(0.1, 0.1)); // about 6 px away
    CHECK(end.snap == SnapMode::Endpoint);
    CHECK(near(end.point, Vec3(0, 0, 0)));

    const auto middle = resolved(drawing, topView(2.6, 0.1));
    CHECK(middle.snap == SnapMode::Midpoint);
    CHECK(near(middle.point, Vec3(2.5, 0, 0)));

    // 1 unit = 40 px: out of the 10 px aperture, the plane point.
    const auto plane = resolved(drawing, topView(1.0, 1.0));
    CHECK_FALSE(plane.snap.has_value());
    CHECK(near(plane.point, Vec3(1, 1, 0)));
}

// REQ-005-19: intersection of crossing segments, also in 3D, and not of
// segments that only cross on the screen.
TEST_CASE("snaps to intersections in space", "[geom][snap]")
{
    const Vec3 a(0, 0, 0);
    const Vec3 b(4, 4, 0);
    SECTION("in the plane")
    {
        Drawing drawing;
        addLines(drawing, {{a, b}, {Vec3(0, 2, 0), Vec3(3, -1, 0)}});
        const auto p = resolved(drawing, topView(1.05, 1.05));
        CHECK(p.snap == SnapMode::Intersection);
        CHECK(near(p.point, Vec3(1, 1, 0)));
    }
    SECTION("crossing in 3D")
    {
        Drawing drawing;
        addLines(drawing, {{a, b}, {Vec3(0, 2, 1), Vec3(3, -1, -2)}});
        const auto p = resolved(drawing, topView(1.05, 1.05));
        CHECK(p.snap == SnapMode::Intersection);
        CHECK(near(p.point, Vec3(1, 1, 0)));
    }
    SECTION("passing above without touching")
    {
        Drawing drawing;
        addLines(drawing, {{a, b}, {Vec3(0, 2, 1), Vec3(3, -1, 1)}});
        const auto p = resolved(drawing, topView(1.05, 1.05));
        CHECK_FALSE(p.snap.has_value());
    }
}

// REQ-005-19, 21: modes and the global switch.
TEST_CASE("snap modes can be turned off", "[geom][snap]")
{
    Drawing drawing;
    addLines(drawing, {{Vec3(0, 0, 0), Vec3(5, 0, 0)}});
    DraftingSettings settings;
    settings.modes = {false, true, true};
    CHECK_FALSE(resolved(drawing, topView(0.1, 0.1), settings).snap.has_value());
    settings = {};
    settings.objectSnap = false;
    CHECK_FALSE(resolved(drawing, topView(2.6, 0.1), settings).snap.has_value());

    // Endpoint wins a tie with the midpoint of a degenerate segment.
    Drawing dot;
    addLines(dot, {{Vec3(1, 1, 0), Vec3(1, 1, 0)}});
    CHECK(resolved(dot, topView(1.0, 1.0)).snap == SnapMode::Endpoint);
}

// REQ-005-22: grid rounding, below the object snap.
TEST_CASE("grid snap rounds to the step", "[geom][snap]")
{
    DraftingSettings settings;
    settings.gridSnap = true;
    settings.gridStep = 0.5;
    const Drawing empty;
    CHECK(near(resolved(empty, topView(1.3, 2.2), settings).point, Vec3(1.5, 2.0, 0)));

    Drawing drawing;
    addLines(drawing, {{Vec3(0.3, 0.3, 0), Vec3(5, 0.3, 0)}});
    const auto p = resolved(drawing, topView(0.35, 0.35), settings);
    CHECK(p.snap == SnapMode::Endpoint);
    CHECK(near(p.point, Vec3(0.3, 0.3, 0)));
}

// REQ-005-23: ortho along X or Y from the last point, on its plane, below the
// object snap.
TEST_CASE("ortho constrains to X or Y", "[geom][snap]")
{
    DraftingSettings settings;
    settings.ortho = true;
    const Drawing empty;
    CHECK(near(resolved(empty, topView(3, 1), settings, Vec3(0, 0, 0)).point, Vec3(3, 0, 0)));
    CHECK(near(resolved(empty, topView(1, 3), settings, Vec3(0, 0, 0)).point, Vec3(0, 3, 0)));
    CHECK(near(resolved(empty, topView(3, 1), settings, Vec3(0, 0, 2)).point, Vec3(3, 0, 2)));
    // Without a last point ortho does nothing.
    CHECK(near(resolved(empty, topView(3, 1), settings).point, Vec3(3, 1, 0)));

    Drawing drawing;
    addLines(drawing, {{Vec3(2, 2, 0), Vec3(5, 5, 0)}});
    const auto p = resolved(drawing, topView(2.1, 2.1), settings, Vec3(0, 0, 0));
    CHECK(p.snap == SnapMode::Endpoint);
    CHECK(near(p.point, Vec3(2, 2, 0)));
}

// REQ-005-14: a ray parallel to the plane gives no point without a snap.
TEST_CASE("no point when the cursor ray misses the plane", "[geom][snap]")
{
    ScreenView view = topView(0, 0);
    view.rayDirection = Vec3::UnitY();
    const PointResolver resolver;
    CHECK_FALSE(resolver.resolve(Drawing{}, view, {}, std::nullopt).has_value());
}

TEST_CASE("closest points of two segments", "[geom][snap]")
{
    const auto crossing =
        fem::geom::closestPoints(Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(1, -1, 1), Vec3(1, 1, 1));
    CHECK(std::abs(crossing.distance - 1.0) < kTolerance);
    CHECK(near(crossing.onFirst, Vec3(1, 0, 0)));
    CHECK(near(crossing.onSecond, Vec3(1, 0, 1)));
    // Parallel and apart.
    const auto parallel =
        fem::geom::closestPoints(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 2, 0), Vec3(1, 2, 0));
    CHECK(std::abs(parallel.distance - 2.0) < kTolerance);
}
