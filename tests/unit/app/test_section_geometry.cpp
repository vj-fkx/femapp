#include "section_geometry.hpp"

#include <fem/core/section.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <variant>

// Feature 010, T16: REQ-010-05, 06. The outline drawn is the section the
// core computes, and a solid polygon comes from closed polylines.

namespace {

namespace c = fem::core;
using Eigen::Vector2d;

// The area of the outline: the rings of material minus the holes.
double areaOf(const std::vector<fem::app::Ring>& rings)
{
    double total = 0.0;
    for (const auto& ring : rings) {
        double area = 0.0;
        for (std::size_t i = 0; i < ring.points.size(); ++i) {
            const Vector2d& a = ring.points[i];
            const Vector2d& b = ring.points[(i + 1) % ring.points.size()];
            area += a.x() * b.y() - b.x() * a.y();
        }
        total += (ring.hole ? -1.0 : 1.0) * std::abs(area) / 2.0;
    }
    return total;
}

double coreArea(const c::SectionShape& shape)
{
    const auto p = c::sectionProperties({.name = "s", .shape = shape});
    REQUIRE(p.has_value());
    return p->area;
}

} // namespace

TEST_CASE("the outline of each shape has the area of the core", "[app][section]")
{
    for (const c::SectionShape& shape :
         {c::SectionShape{c::Rectangle{.b = 0.3, .h = 0.5}},
          c::SectionShape{c::RectangularTube{.b = 0.2, .h = 0.3, .t = 0.01}},
          c::SectionShape{c::ISection{.h = 0.4, .bf = 0.2, .tf = 0.015, .tw = 0.01}},
          c::SectionShape{c::Angle{.b = 0.1, .h = 0.15, .t = 0.01}},
          c::SectionShape{c::Channel{.h = 0.3, .bf = 0.1, .tf = 0.012, .tw = 0.008}},
          c::SectionShape{c::Tee{.h = 0.25, .bf = 0.2, .tf = 0.02, .tw = 0.01}}}) {
        INFO(shape.index());
        CHECK(std::abs(areaOf(fem::app::outlineOf(shape)) - coreArea(shape)) <=
              1e-12 * coreArea(shape));
    }
    // Circles as 64-sided polygons: within 0.2% of the area.
    const c::SectionShape tube = c::CircularTube{.d = 0.3, .t = 0.02};
    CHECK(std::abs(areaOf(fem::app::outlineOf(tube)) / coreArea(tube) - 1.0) <= 2e-3);
}

TEST_CASE("the walls of a thin-walled section are drawn with their thickness", "[app][section]")
{
    const c::ThinWalled thin{.points = {{0.0, 0.0}, {0.2, 0.0}, {0.2, 0.3}},
                             .walls = {{.from = 0, .to = 1, .t = 0.01},
                                       {.from = 1, .to = 2, .t = 0.02},
                                       {.from = 2, .to = 7, .t = 0.01}}}; // a bad index
    const auto rings = fem::app::outlineOf(thin);
    REQUIRE(rings.size() == 2);
    CHECK(std::abs(areaOf(rings) - (0.2 * 0.01 + 0.3 * 0.02)) <= 1e-15);
}

TEST_CASE("the principal axes back in the drawing", "[app][section]")
{
    c::SectionProperties p;
    p.centroid = Vector2d(1.0, 2.0);
    p.principalAngle = std::numbers::pi / 2;
    // The principal y' along the drawing z.
    const Vector2d d = fem::app::toDrawing(p, Vector2d(1.0, 0.0));
    CHECK((d - Vector2d(1.0, 3.0)).norm() <= 1e-15);
}

TEST_CASE("a solid polygon from closed polylines", "[app][section]")
{
    fem::geom::Drawing drawing;
    const auto add = [&](std::vector<fem::geom::Vec3> vertices, bool closed) {
        const fem::geom::EntityId id = drawing.nextId();
        fem::geom::DrawingEdit edit;
        edit.added.push_back(fem::geom::Entity{
            .id = id,
            .shape = fem::geom::PolylineEntity{.vertices = std::move(vertices), .closed = closed}});
        drawing.apply(edit);
        return id;
    };
    const auto hole = add({{0.1, 0.1, 0}, {0.2, 0.1, 0}, {0.2, 0.2, 0}, {0.1, 0.2, 0}}, true);
    const auto outer = add({{0, 0, 0}, {0.4, 0, 0}, {0.4, 0.3, 0}, {0, 0.3, 0}}, true);
    const auto open = add({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}, false);

    const std::array<fem::geom::EntityId, 2> both{hole, outer};
    const auto made = fem::app::solidFromPolylines(drawing, both);
    REQUIRE(std::holds_alternative<c::SolidPolygon>(made));
    const auto& solid = std::get<c::SolidPolygon>(made);
    REQUIRE(solid.regions.size() == 1);
    CHECK(solid.regions.front().outline.size() == 4);
    CHECK(solid.regions.front().outline[1] == Vector2d(0.4, 0.0));
    REQUIRE(solid.regions.front().holes.size() == 1);
    // The core takes it: 0.12 - 0.01.
    CHECK(std::abs(coreArea(solid) - 0.11) <= 1e-12);

    const std::array<fem::geom::EntityId, 1> wrong{open};
    CHECK(std::holds_alternative<std::string>(fem::app::solidFromPolylines(drawing, wrong)));
    CHECK(std::holds_alternative<std::string>(fem::app::solidFromPolylines(drawing, {})));
}
