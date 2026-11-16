// REQ-008b-02, 04: the triangulation of solid polygon sections (design 008b
// §5.1.3, ADR 0005), with the checks the constitution asks of mesh
// generators (§7.4): conformity, minimum quality and degenerate cases.

#include <fem/core/detail/section_mesh.hpp>
#include <fem/core/section.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using fem::core::SolidPolygon;
using fem::core::detail::meshSection;
using fem::core::detail::SectionMesh;
using Vec = Eigen::Vector2d;
using Ring = std::vector<Vec>;

namespace {

constexpr double kPi = std::numbers::pi;

double cross(const Vec& a, const Vec& b)
{
    return a.x() * b.y() - a.y() * b.x();
}

SectionMesh mesh(const SolidPolygon& shape)
{
    auto result = meshSection(shape, "S");
    REQUIRE(result.has_value());
    return result.value_or(SectionMesh{});
}

std::string failure(const SolidPolygon& shape)
{
    const auto result = meshSection(shape, "S");
    REQUIRE_FALSE(result.has_value());
    return result.error().message;
}

Ring rectangle(double y0, double z0, double y1, double z1)
{
    return {Vec(y0, z0), Vec(y1, z0), Vec(y1, z1), Vec(y0, z1)};
}

Ring circle(double r, int n, const Vec& centre = Vec::Zero())
{
    Ring ring;
    for (int i = 0; i < n; ++i) {
        const double angle = 2 * kPi * i / n;
        ring.emplace_back(centre + Vec(r * std::cos(angle), r * std::sin(angle)));
    }
    return ring;
}

double perimeter(const SolidPolygon& shape)
{
    double length = 0.0;
    const auto add = [&length](const Ring& ring) {
        for (std::size_t i = 0; i < ring.size(); ++i) {
            length += (ring[(i + 1) % ring.size()] - ring[i]).norm();
        }
    };
    for (const auto& region : shape.regions) {
        add(region.outline);
        for (const Ring& hole : region.holes) {
            add(hole);
        }
    }
    return length;
}

double smallestAngle(const SectionMesh& m)
{
    double smallest = kPi;
    for (const auto& t : m.triangles) {
        for (int k = 0; k < 3; ++k) {
            const Vec& a = m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>(k)))];
            const Vec& b =
                m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>((k + 1) % 3)))];
            const Vec& c =
                m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>((k + 2) % 3)))];
            const double angle =
                std::acos(std::clamp((b - a).normalized().dot((c - a).normalized()), -1.0, 1.0));
            smallest = std::min(smallest, angle);
        }
    }
    return smallest;
}

double longestEdge(const SectionMesh& m)
{
    double longest = 0.0;
    for (const auto& t : m.triangles) {
        for (int k = 0; k < 3; ++k) {
            longest = std::max(
                longest,
                (m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>((k + 1) % 3)))] -
                 m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>(k)))])
                    .norm());
        }
    }
    return longest;
}

// Counterclockwise triangles that cover the polygons exactly, every edge on
// one or two triangles, the edges on one triangle making the boundary, and
// the mid-side nodes in the middle of their edges and shared.
void checkConforming(const SolidPolygon& shape, const SectionMesh& m)
{
    double area = 0.0;
    std::map<std::pair<int, int>, int> edges;
    std::map<std::pair<int, int>, int> middles;
    for (const auto& t : m.triangles) {
        const auto node = [&m, &t](int k) {
            return m.nodes[static_cast<std::size_t>(t.at(static_cast<std::size_t>(k)))];
        };
        const double twice = cross(node(1) - node(0), node(2) - node(0));
        CHECK(twice > 0.0);
        area += twice / 2;
        for (int k = 0; k < 3; ++k) {
            const int a = t.at(static_cast<std::size_t>(k));
            const int b = t.at(static_cast<std::size_t>((k + 1) % 3));
            const auto key = std::minmax(a, b);
            ++edges[key];
            const int middle = t.at(3 + static_cast<std::size_t>(k));
            CHECK((node(3 + k) - (node(k) + node((k + 1) % 3)) / 2).norm() <= 1e-15);
            const auto [it, added] = middles.try_emplace(key, middle);
            CHECK(it->second == middle);
        }
    }
    CHECK(std::abs(area - fem::core::detail::polygonArea(shape)) <=
          1e-12 * fem::core::detail::polygonArea(shape));
    double boundary = 0.0;
    for (const auto& [key, count] : edges) {
        CHECK((count == 1 || count == 2));
        if (count == 1) {
            boundary += (m.nodes[static_cast<std::size_t>(key.second)] -
                         m.nodes[static_cast<std::size_t>(key.first)])
                            .norm();
        }
    }
    CHECK(std::abs(boundary - perimeter(shape)) <= 1e-12 * perimeter(shape));
}

// Constrained Delaunay: no corner node inside the circumcircle of a
// triangle through an edge it shares with another.
void checkDelaunay(const SectionMesh& m)
{
    std::map<std::pair<int, int>, std::vector<std::size_t>> owners;
    for (std::size_t t = 0; t < m.triangles.size(); ++t) {
        for (int k = 0; k < 3; ++k) {
            const auto& tri = m.triangles[t];
            owners[std::minmax(tri.at(static_cast<std::size_t>(k)),
                               tri.at(static_cast<std::size_t>((k + 1) % 3)))]
                .push_back(t);
        }
    }
    for (const auto& [edge, list] : owners) {
        if (list.size() != 2) {
            continue;
        }
        const auto& t1 = m.triangles[list[0]];
        const auto& t2 = m.triangles[list[1]];
        int d = -1;
        for (int k = 0; k < 3; ++k) {
            const int v = t2.at(static_cast<std::size_t>(k));
            if (v != edge.first && v != edge.second) {
                d = v;
            }
        }
        const auto node = [&m](int i) { return m.nodes[static_cast<std::size_t>(i)]; };
        const Vec a = node(t1[0]) - node(d);
        const Vec b = node(t1[1]) - node(d);
        const Vec c = node(t1[2]) - node(d);
        const double det = a.squaredNorm() * cross(b, c) - b.squaredNorm() * cross(a, c) +
                           c.squaredNorm() * cross(a, b);
        const double scale = std::pow(std::max({a.norm(), b.norm(), c.norm()}), 4);
        CHECK(det <= 1e-9 * scale);
    }
}

} // namespace

TEST_CASE("the section triangulation is conforming", "[core][section][mesh]")
{
    SECTION("rectangle")
    {
        const SolidPolygon shape{.regions = {{.outline = rectangle(0, 0, 0.5, 0.2), .holes = {}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("L shape, clockwise")
    {
        Ring l = {Vec(0, 0),       Vec(0.3, 0),    Vec(0.3, 0.05),
                  Vec(0.05, 0.05), Vec(0.05, 0.4), Vec(0, 0.4)};
        std::ranges::reverse(l);
        const SolidPolygon shape{.regions = {{.outline = l, .holes = {}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("a comb: many reflex vertices")
    {
        Ring comb = {Vec(0, 0), Vec(1, 0)};
        for (int i = 9; i >= 0; --i) {
            comb.emplace_back(0.1 * (i + 1), 0.5);
            comb.emplace_back(0.1 * i + 0.05, 0.5);
            comb.emplace_back(0.1 * i + 0.05, 0.1);
            comb.emplace_back(0.1 * i, 0.1);
        }
        comb.pop_back(); // (0, 0.1) is on the edge back to (0, 0)
        comb.emplace_back(0.0, 0.1);
        const SolidPolygon shape{.regions = {{.outline = comb, .holes = {}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("tube: a circle with a circular hole")
    {
        const SolidPolygon shape{
            .regions = {{.outline = circle(0.1, 64), .holes = {circle(0.09, 48)}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("a plate with three holes")
    {
        const SolidPolygon shape{
            .regions = {{.outline = rectangle(0, 0, 1.0, 0.3),
                         .holes = {rectangle(0.1, 0.1, 0.2, 0.2), circle(0.06, 20, Vec(0.5, 0.15)),
                                   rectangle(0.75, 0.05, 0.9, 0.25)}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("two regions, one inside the hole of the other")
    {
        const SolidPolygon shape{
            .regions = {
                {.outline = rectangle(0, 0, 1, 1), .holes = {rectangle(0.2, 0.2, 0.8, 0.8)}},
                {.outline = rectangle(0.4, 0.4, 0.6, 0.6), .holes = {}},
                {.outline = rectangle(2, 0, 3, 1), .holes = {}}}};
        checkConforming(shape, mesh(shape));
    }
    SECTION("an I with vertices on straight lines")
    {
        const SolidPolygon shape{
            .regions = {{.outline = {Vec(-0.15, -0.075), Vec(-0.15, 0.0), Vec(-0.15, 0.075),
                                     Vec(-0.14, 0.075), Vec(-0.14, 0.0035), Vec(0.14, 0.0035),
                                     Vec(0.14, 0.075), Vec(0.15, 0.075), Vec(0.15, -0.075),
                                     Vec(0.14, -0.075), Vec(0.14, -0.0035), Vec(-0.14, -0.0035),
                                     Vec(-0.14, -0.075)},
                         .holes = {}}}};
        checkConforming(shape, mesh(shape));
    }
}

TEST_CASE("the section triangulation keeps its quality", "[core][section][mesh]")
{
    const SolidPolygon coarse{
        .regions = {{.outline = circle(0.1, 40), .holes = {circle(0.05, 24)}}}, .meshSize = 1.0};
    const SectionMesh initial = mesh(coarse);
    checkDelaunay(initial);

    SolidPolygon fine = coarse;
    fine.meshSize = 0.004;
    const SectionMesh refined = mesh(fine);
    checkConforming(fine, refined);
    CHECK(longestEdge(refined) <= 0.004);
    // Rivara: the smallest angle stays above half of the initial one.
    CHECK(smallestAngle(refined) >= smallestAngle(initial) / 2 - 1e-12);
    CHECK(refined.triangles.size() > 10 * initial.triangles.size());

    SECTION("the default size gives about 3000 triangles")
    {
        const SolidPolygon square{.regions = {{.outline = rectangle(0, 0, 1, 1), .holes = {}}}};
        const auto count = static_cast<double>(mesh(square).triangles.size());
        CHECK(count > 1000.0);
        CHECK(count < 12000.0);
    }
}

TEST_CASE("invalid polygons are refused", "[core][section][mesh]")
{
    const std::string p = "Section \"S\": ";
    CHECK(failure(SolidPolygon{}) == p + "a polygon section needs at least one region.");
    CHECK(failure({.regions = {{.outline = {Vec(0, 0), Vec(1, 0)}, .holes = {}}}}) ==
          p + "the outline of the region 0 has fewer than three vertices.");
    CHECK(failure({.regions = {{.outline = {Vec(0, 0), Vec(1, 0), Vec(2, 0)}, .holes = {}}}}) ==
          p + "the outline of the region 0 has zero area.");
    CHECK(failure({.regions = {{.outline = {Vec(0, 0), Vec(2, 2), Vec(2, 0), Vec(0, 1)},
                                .holes = {}}}}) ==
          p + "the outline of the region 0 crosses itself at the vertex 2.");
    CHECK(failure({.regions = {{.outline = {Vec(0, 0), Vec(1, 0), Vec(1, 0), Vec(0, 1)},
                                .holes = {}}}}) ==
          p + "the outline of the region 0 repeats the vertex 1.");
    CHECK(failure({.regions = {{.outline = rectangle(0, 0, 1, 1),
                                .holes = {rectangle(2, 2, 3, 3)}}}}) ==
          p + "the hole 0 of the region 0 is not inside its outline.");
    CHECK(failure({.regions = {{.outline = rectangle(0, 0, 1, 1),
                                .holes = {rectangle(0, 0.2, 0.5, 0.5)}}}}) ==
          p + "the hole 0 of the region 0 is not inside its outline.");
    CHECK(failure({.regions = {{.outline = rectangle(0, 0, 1, 1),
                                .holes = {rectangle(0.1, 0.1, 0.5, 0.5),
                                          rectangle(0.4, 0.4, 0.8, 0.8)}}}}) ==
          p + "the holes 0 and 1 of the region 0 overlap.");
    CHECK(failure({.regions = {{.outline = rectangle(0, 0, 1, 1), .holes = {}},
                               {.outline = rectangle(0.5, 0.5, 2, 2), .holes = {}}}}) ==
          p + "the regions 0 and 1 overlap.");
    CHECK(failure({.regions = {{.outline = rectangle(0, 0, 1, 1), .holes = {}},
                               {.outline = rectangle(0.2, 0.2, 0.4, 0.4), .holes = {}}}}) ==
          p + "the regions 0 and 1 overlap.");
    CHECK(
        failure({.regions = {{.outline = rectangle(0, 0, 1, 1), .holes = {}}}, .meshSize = -1.0}) ==
        p + "the mesh size must be positive, or 0 for the default.");
    CHECK(failure({.regions = {{.outline = {Vec(0, 0), Vec(1, 0), Vec(NAN, 1)}, .holes = {}}}}) ==
          p + "the vertex 2 of the outline of the region 0 is not finite.");
}
