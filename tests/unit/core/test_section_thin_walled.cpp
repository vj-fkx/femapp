// REQ-008b-03, 04: thin-walled sections by their mid-line (design 008b
// §5.1.2), against the classic thin-walled formulas.

#include <fem/core/beam_result.hpp>
#include <fem/core/section.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <Eigen/LU>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using fem::core::Section;
using fem::core::SectionProperties;
using fem::core::ThinWalled;
using Wall = fem::core::ThinWalled::Wall;
using Vec = Eigen::Vector2d;

namespace {

constexpr double kPi = std::numbers::pi;

bool near(double value, double reference, double tolerance)
{
    return std::abs(value - reference) <= tolerance * std::max(1.0e-300, std::abs(reference));
}

SectionProperties of(const ThinWalled& shape)
{
    Section section{.name = "T"};
    section.shape = shape;
    const auto properties = fem::core::sectionProperties(section);
    REQUIRE(properties.has_value());
    return properties.value_or(SectionProperties{});
}

std::string failure(const ThinWalled& shape)
{
    Section section{.name = "T"};
    section.shape = shape;
    const auto properties = fem::core::sectionProperties(section);
    REQUIRE_FALSE(properties.has_value());
    return properties.error().message;
}

// The shear centre in the drawing, absolute.
Vec shearCentre(const SectionProperties& p)
{
    const double c = std::cos(p.principalAngle);
    const double s = std::sin(p.principalAngle);
    const Vec& r = p.shearCentre;
    return p.centroid + Vec(c * r.x() - s * r.y(), s * r.x() + c * r.y());
}

ThinWalled channel(double b, double h0, double tf, double tw)
{
    return {.points = {Vec(h0 / 2, b), Vec(h0 / 2, 0.0), Vec(-h0 / 2, 0.0), Vec(-h0 / 2, b)},
            .walls = {Wall{.from = 0, .to = 1, .t = tf}, Wall{.from = 1, .to = 2, .t = tw},
                      Wall{.from = 2, .to = 3, .t = tf}}};
}

ThinWalled box(double b0, double h0, double t)
{
    return {.points = {Vec(h0 / 2, -b0 / 2), Vec(h0 / 2, b0 / 2), Vec(-h0 / 2, b0 / 2),
                       Vec(-h0 / 2, -b0 / 2)},
            .walls = {Wall{.from = 0, .to = 1, .t = t}, Wall{.from = 1, .to = 2, .t = t},
                      Wall{.from = 2, .to = 3, .t = t}, Wall{.from = 3, .to = 0, .t = t}}};
}

} // namespace

TEST_CASE("open thin-walled sections", "[core][section][thin]")
{
    SECTION("I: the mid-line formulas")
    {
        const double bf = 0.2;
        const double h0 = 0.4;
        const double tf = 0.015;
        const double tw = 0.01;
        const ThinWalled shape{
            .points = {Vec(h0 / 2, -bf / 2), Vec(h0 / 2, 0.0), Vec(h0 / 2, bf / 2),
                       Vec(-h0 / 2, -bf / 2), Vec(-h0 / 2, 0.0), Vec(-h0 / 2, bf / 2)},
            .walls = {Wall{.from = 0, .to = 1, .t = tf}, Wall{.from = 1, .to = 2, .t = tf},
                      Wall{.from = 3, .to = 4, .t = tf}, Wall{.from = 4, .to = 5, .t = tf},
                      Wall{.from = 1, .to = 4, .t = tw}}};
        const SectionProperties p = of(shape);
        CHECK(near(p.area, 2 * bf * tf + h0 * tw, 1e-12));
        CHECK(near(p.inertiaZ, 2 * bf * tf * h0 * h0 / 4 + tw * std::pow(h0, 3) / 12, 1e-12));
        CHECK(near(p.inertiaY, 2 * tf * std::pow(bf, 3) / 12, 1e-12));
        CHECK(
            near(p.torsionConstant, (2 * bf * std::pow(tf, 3) + h0 * std::pow(tw, 3)) / 3, 1e-12));
        CHECK(near(p.warpingConstant, tf * std::pow(bf, 3) * h0 * h0 / 24, 1e-12));
        CHECK(p.shearCentre.norm() <= 1e-12 * h0);
        // The same Iw as the parametric I on the same mid-lines.
        const auto parametric = fem::core::sectionProperties(
            {.name = "P",
             .shape = fem::core::ISection{.h = h0 + tf, .bf = bf, .tf = tf, .tw = tw}});
        REQUIRE(parametric.has_value());
        CHECK(near(p.warpingConstant, parametric.value_or(SectionProperties{}).warpingConstant,
                   1e-12));
    }

    SECTION("channel: shear centre and Iw of Galambos")
    {
        const double b = 0.07;
        const double h0 = 0.19;
        const double tf = 0.0115;
        const double tw = 0.0085;
        const SectionProperties p = of(channel(b, h0, tf, tw));
        const double e = 3 * b * b * tf / (6 * b * tf + h0 * tw);
        CHECK(near(shearCentre(p).y(), -e, 1e-12));
        CHECK(std::abs(shearCentre(p).x()) <= 1e-12 * h0);
        CHECK(near(p.warpingConstant,
                   tf * std::pow(b, 3) * h0 * h0 / 12 * (3 * b * tf + 2 * h0 * tw) /
                       (6 * b * tf + h0 * tw),
                   1e-12));
        CHECK(near(p.torsionConstant, (2 * b * std::pow(tf, 3) + h0 * std::pow(tw, 3)) / 3, 1e-12));
    }

    SECTION("Z: point symmetric, shear centre at the centroid")
    {
        const double b = 0.08;
        const double h0 = 0.25;
        const double tf = 0.006;
        const double tw = 0.004;
        const ThinWalled z{
            .points = {Vec(h0 / 2, b), Vec(h0 / 2, 0.0), Vec(-h0 / 2, 0.0), Vec(-h0 / 2, -b)},
            .walls = {Wall{.from = 0, .to = 1, .t = tf}, Wall{.from = 1, .to = 2, .t = tw},
                      Wall{.from = 2, .to = 3, .t = tf}}};
        const SectionProperties p = of(z);
        CHECK(p.principalAngle != 0.0);
        CHECK(p.shearCentre.norm() <= 1e-12 * h0);
        CHECK(near(p.warpingConstant,
                   tf * std::pow(b, 3) * h0 * h0 * (b * tf + 2 * h0 * tw) /
                       (12 * (2 * b * tf + h0 * tw)),
                   1e-12));
    }

    SECTION("a single plate: As = 5/6 A, no warping")
    {
        const ThinWalled plate{.points = {Vec(-0.15, 0.0), Vec(0.15, 0.0)},
                               .walls = {Wall{.from = 0, .to = 1, .t = 0.01}}};
        const SectionProperties p = of(plate);
        REQUIRE(p.shearAreaY.has_value());
        CHECK(near(p.shearAreaY.value_or(0.0), 5.0 / 6.0 * 0.3 * 0.01, 1e-12));
        CHECK(near(p.torsionConstant, 0.3 * 1e-6 / 3, 1e-12));
        CHECK(p.warpingConstant <= 1e-20);
    }

    SECTION("the order and direction of the walls do not matter")
    {
        // A symmetric hat: lips, webs and top.
        const ThinWalled hat{
            .points = {Vec(0.0, -0.12), Vec(0.0, -0.05), Vec(0.1, -0.05), Vec(0.1, 0.05),
                       Vec(0.0, 0.05), Vec(0.0, 0.12)},
            .walls = {Wall{.from = 0, .to = 1, .t = 0.003}, Wall{.from = 1, .to = 2, .t = 0.003},
                      Wall{.from = 2, .to = 3, .t = 0.004}, Wall{.from = 3, .to = 4, .t = 0.003},
                      Wall{.from = 4, .to = 5, .t = 0.003}}};
        ThinWalled shuffled = hat;
        std::ranges::reverse(shuffled.walls);
        for (Wall& wall : shuffled.walls) {
            std::swap(wall.from, wall.to);
        }
        const SectionProperties a = of(hat);
        const SectionProperties b = of(shuffled);
        CHECK(std::abs(shearCentre(a).y()) <= 1e-12);
        CHECK(near(b.warpingConstant, a.warpingConstant, 1e-12));
        CHECK(near(b.torsionConstant, a.torsionConstant, 1e-12));
        CHECK((shearCentre(b) - shearCentre(a)).norm() <= 1e-12);
        CHECK(near(b.shearAreaY.value_or(0.0), a.shearAreaY.value_or(1.0), 1e-12));
        CHECK(near(b.shearAreaZ.value_or(0.0), a.shearAreaZ.value_or(1.0), 1e-12));
    }

    SECTION("a turned drawing gives the same principal properties")
    {
        const ThinWalled u = channel(0.07, 0.19, 0.0115, 0.0085);
        ThinWalled turned = u;
        const Eigen::Rotation2Dd rotation(0.5);
        for (Vec& point : turned.points) {
            point = rotation * point;
        }
        const SectionProperties a = of(u);
        const SectionProperties b = of(turned);
        CHECK(near(b.inertiaY, a.inertiaY, 1e-10));
        CHECK(near(b.inertiaZ, a.inertiaZ, 1e-10));
        CHECK(near(b.warpingConstant, a.warpingConstant, 1e-10));
        CHECK(near(b.shearAreaY.value_or(0.0), a.shearAreaY.value_or(1.0), 1e-10));
        CHECK(near(b.shearAreaZ.value_or(0.0), a.shearAreaZ.value_or(1.0), 1e-10));
        CHECK((shearCentre(b) - rotation * shearCentre(a)).norm() <= 1e-12);
    }
}

TEST_CASE("closed thin-walled sections", "[core][section][thin]")
{
    const double b0 = 0.29;
    const double h0 = 0.19;
    const double t = 0.01;
    const double bredt = 4 * std::pow(b0 * h0, 2) * t / (2 * (b0 + h0));

    SECTION("one cell: Bredt and the parametric tube")
    {
        const SectionProperties p = of(box(b0, h0, t));
        CHECK(near(p.torsionConstant, bredt, 1e-12));
        CHECK(near(p.warpingConstant,
                   t * b0 * b0 * h0 * h0 * std::pow(b0 - h0, 2) / (24 * (b0 + h0)), 1e-12));
        CHECK(p.shearCentre.norm() <= 1e-12);
        const auto tube = fem::core::sectionProperties(
            {.name = "P", .shape = fem::core::RectangularTube{.b = b0 + t, .h = h0 + t, .t = t}});
        REQUIRE(tube.has_value());
        CHECK(near(p.torsionConstant, tube.value_or(SectionProperties{}).torsionConstant, 1e-12));
        CHECK(near(p.warpingConstant, tube.value_or(SectionProperties{}).warpingConstant, 1e-12));
    }

    SECTION("two symmetric cells: no flow in the middle web")
    {
        ThinWalled twin{.points = {Vec(h0 / 2, -b0 / 2), Vec(h0 / 2, 0.0), Vec(h0 / 2, b0 / 2),
                                   Vec(-h0 / 2, b0 / 2), Vec(-h0 / 2, 0.0), Vec(-h0 / 2, -b0 / 2)},
                        .walls = {}};
        for (int i = 0; i < 6; ++i) {
            twin.walls.push_back(Wall{.from = i, .to = (i + 1) % 6, .t = t});
        }
        twin.walls.push_back(Wall{.from = 1, .to = 4, .t = 2 * t});
        CHECK(near(of(twin).torsionConstant, bredt, 1e-12));
    }

    SECTION("two unequal cells: generalised Bredt")
    {
        const double c = 0.06; // middle web at z = c
        const double tm = 0.02;
        ThinWalled twin{.points = {Vec(h0 / 2, -b0 / 2), Vec(h0 / 2, c), Vec(h0 / 2, b0 / 2),
                                   Vec(-h0 / 2, b0 / 2), Vec(-h0 / 2, c), Vec(-h0 / 2, -b0 / 2)},
                        .walls = {}};
        for (int i = 0; i < 6; ++i) {
            twin.walls.push_back(Wall{.from = i, .to = (i + 1) % 6, .t = t});
        }
        twin.walls.push_back(Wall{.from = 1, .to = 4, .t = tm});
        const double a1 = h0 * (c + b0 / 2);
        const double a2 = h0 * (b0 / 2 - c);
        Eigen::Matrix2d f;
        f << (2 * (c + b0 / 2) + h0) / t + h0 / tm, -h0 / tm, -h0 / tm,
            (2 * (b0 / 2 - c) + h0) / t + h0 / tm;
        const Eigen::Vector2d areas(2 * a1, 2 * a2);
        const Eigen::Vector2d q = f.inverse() * areas;
        CHECK(near(of(twin).torsionConstant, q.dot(areas), 1e-12));
    }

    SECTION("a thin circular tube as a polygon")
    {
        const int n = 256;
        const double r = 0.1;
        const double wall = 0.002;
        ThinWalled tube;
        for (int i = 0; i < n; ++i) {
            const double angle = 2 * kPi * i / n;
            tube.points.emplace_back(r * std::cos(angle), r * std::sin(angle));
            tube.walls.push_back(Wall{.from = i, .to = (i + 1) % n, .t = wall});
        }
        const SectionProperties p = of(tube);
        CHECK(near(p.torsionConstant, 2 * kPi * std::pow(r, 3) * wall, 1e-3));
        CHECK(near(p.shearAreaY.value_or(0.0), p.area / 2, 1e-3));
        CHECK(near(p.shearAreaZ.value_or(0.0), p.area / 2, 1e-3));
        CHECK(p.warpingConstant <= 1e-9 * p.torsionConstant * r * r);
        CHECK(p.shearCentre.norm() <= 1e-12);
    }
}

TEST_CASE("invalid thin-walled sections are refused", "[core][section][thin]")
{
    CHECK(failure(ThinWalled{}) == "Section \"T\": a thin-walled section needs at least one wall.");
    ThinWalled u = channel(0.07, 0.19, 0.0115, 0.0085);
    u.walls[1].t = 0.0;
    CHECK(failure(u) == "Section \"T\": the thickness of the wall 1 must be positive.");
    u = channel(0.07, 0.19, 0.0115, 0.0085);
    u.walls[2].to = 2;
    CHECK(failure(u) == "Section \"T\": the wall 2 has zero length.");
    u = channel(0.07, 0.19, 0.0115, 0.0085);
    u.walls[0].to = 7;
    CHECK(failure(u) == "Section \"T\": the wall 0 refers to a missing point.");
    u = channel(0.07, 0.19, 0.0115, 0.0085);
    u.points.push_back(u.points[1]);
    CHECK(failure(u) == "Section \"T\": the points 1 and 4 coincide.");
    u = channel(0.07, 0.19, 0.0115, 0.0085);
    u.points.emplace_back(1.0, 1.0);
    u.points.emplace_back(1.0, 2.0);
    u.walls.push_back(Wall{.from = 4, .to = 5, .t = 0.01});
    CHECK(failure(u) == "Section \"T\": the walls are not connected (the wall 3 is apart).");
}

namespace {

fem::core::StressPoint pointNamed(const ThinWalled& shape, const std::string& name)
{
    const auto points = fem::core::stressPoints(shape);
    const auto found = std::ranges::find_if(
        points, [&name](const fem::core::StressPoint& p) { return p.name == name; });
    REQUIRE(found != points.end());
    return *found;
}

fem::core::PointStress stressNamed(const ThinWalled& shape, const fem::core::BeamForces& forces,
                                   const std::string& name)
{
    const auto stresses = fem::core::sectionStresses({.name = "T", .shape = shape}, forces);
    const auto found = std::ranges::find_if(
        stresses, [&name](const fem::core::PointStress& p) { return p.name == name; });
    REQUIRE(found != stresses.end());
    return *found;
}

} // namespace

// REQ-008b-01, 03: the sectorial coordinate of the mid-line agrees with the
// parametric shapes, signs included.
TEST_CASE("sectorial coordinates of thin-walled sections", "[core][section][thin]")
{
    const auto parametric = [](const fem::core::SectionShape& shape, const std::string& name) {
        const auto points = fem::core::stressPoints(shape);
        const auto found = std::ranges::find_if(
            points, [&name](const fem::core::StressPoint& p) { return p.name == name; });
        REQUIRE(found != points.end());
        return found->sectorial;
    };
    const double b = 0.07;
    const double h0 = 0.19;
    const double tf = 0.0115;
    const double tw = 0.0085;
    const fem::core::Channel u{.h = h0 + tf, .bf = b + tw / 2, .tf = tf, .tw = tw};
    CHECK(near(pointNamed(channel(b, h0, tf, tw), "wall 0 start, face +").sectorial,
               parametric(u, "flange tip +y"), 1e-12));
    CHECK(near(pointNamed(channel(b, h0, tf, tw), "wall 0 end, face +").sectorial,
               parametric(u, "web back +y"), 1e-12));

    const double b0 = 0.29;
    const double h0box = 0.19;
    const fem::core::RectangularTube tube{.b = b0 + 0.01, .h = h0box + 0.01, .t = 0.01};
    // Point 0 is (h0/2, -b0/2).
    CHECK(near(pointNamed(box(b0, h0box, 0.01), "wall 0 start, face +").sectorial,
               parametric(tube, "corner +y -z"), 1e-12));
}

// REQ-008b-06: the stresses at both faces of the walls.
TEST_CASE("stresses of thin-walled sections", "[core][section][thin]")
{
    const double bf = 0.2;
    const double h0 = 0.4;
    const double tf = 0.015;
    const double tw = 0.01;
    const ThinWalled i{
        .points = {Vec(h0 / 2, -bf / 2), Vec(h0 / 2, 0.0), Vec(h0 / 2, bf / 2),
                   Vec(-h0 / 2, -bf / 2), Vec(-h0 / 2, 0.0), Vec(-h0 / 2, bf / 2)},
        .walls = {Wall{.from = 0, .to = 1, .t = tf}, Wall{.from = 1, .to = 2, .t = tf},
                  Wall{.from = 3, .to = 4, .t = tf}, Wall{.from = 4, .to = 5, .t = tf},
                  Wall{.from = 1, .to = 4, .t = tw}}};
    const SectionProperties p = of(i);
    const fem::core::BeamForces shear{.vy = 1000.0};
    // The web takes the flow of both half flanges at the junction.
    CHECK(near(stressNamed(i, shear, "wall 4 start, face +").shear,
               1000.0 * bf * tf * h0 / 2 / (p.inertiaZ * tw), 1e-12));
    CHECK(stressNamed(i, shear, "wall 0 start, face -").shear <= 1e-9);
    const fem::core::BeamForces torque{.t = 50.0};
    CHECK(near(stressNamed(i, torque, "wall 1 end, face +").shear, 50.0 * tf / p.torsionConstant,
               1e-12));
    const fem::core::BeamForces bending{.n = 1e4, .mz = 2e3};
    const auto corner = stressNamed(i, bending, "wall 1 end, face +");
    CHECK(near(corner.normal, 1e4 / p.area - 2e3 * corner.position.x() / p.inertiaZ, 1e-12));

    // Bredt: T / (2 Am t) all around the box.
    const ThinWalled tube = box(0.29, 0.19, 0.01);
    CHECK(near(stressNamed(tube, torque, "wall 2 start, face -").shear,
               50.0 / (2 * 0.29 * 0.19 * 0.01), 1e-12));
}
