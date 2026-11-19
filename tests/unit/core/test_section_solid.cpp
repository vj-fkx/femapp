// REQ-008b-02, NFR-008b-02: solid polygon sections (design 008b §5.1.3).

#include <fem/core/beam_result.hpp>
#include <fem/core/section.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using fem::core::Section;
using fem::core::SectionProperties;
using fem::core::SolidPolygon;
using fem::core::ThinWalled;
using Wall = fem::core::ThinWalled::Wall;
using Vec = Eigen::Vector2d;
using Ring = std::vector<Vec>;

namespace {

constexpr double kPi = std::numbers::pi;

bool near(double value, double reference, double tolerance)
{
    return std::abs(value - reference) <= tolerance * std::max(1.0e-300, std::abs(reference));
}

SectionProperties of(const fem::core::SectionShape& shape)
{
    Section section{.name = "S"};
    section.shape = shape;
    const auto properties = fem::core::sectionProperties(section);
    REQUIRE(properties.has_value());
    return properties.value_or(SectionProperties{});
}

Ring rectangle(double y0, double z0, double y1, double z1)
{
    return {Vec(y0, z0), Vec(y1, z0), Vec(y1, z1), Vec(y0, z1)};
}

Ring circle(double r, int n)
{
    Ring ring;
    for (int i = 0; i < n; ++i) {
        const double angle = 2 * kPi * i / n;
        ring.emplace_back(r * std::cos(angle), r * std::sin(angle));
    }
    return ring;
}

SolidPolygon solid(Ring outline, std::vector<Ring> holes = {}, double size = 0.0)
{
    return {.regions = {{.outline = std::move(outline), .holes = std::move(holes)}},
            .meshSize = size};
}

// The shear centre in the drawing, absolute.
Vec shearCentre(const SectionProperties& p)
{
    const double c = std::cos(p.principalAngle);
    const double s = std::sin(p.principalAngle);
    const Vec& r = p.shearCentre;
    return p.centroid + Vec(c * r.x() - s * r.y(), s * r.x() + c * r.y());
}

ThinWalled ipeMidLine(double h, double bf, double tf, double tw)
{
    const double y = (h - tf) / 2;
    return {.points = {Vec(y, -bf / 2), Vec(y, 0.0), Vec(y, bf / 2), Vec(-y, -bf / 2), Vec(-y, 0.0),
                       Vec(-y, bf / 2)},
            .walls = {Wall{.from = 0, .to = 1, .t = tf}, Wall{.from = 1, .to = 2, .t = tf},
                      Wall{.from = 3, .to = 4, .t = tf}, Wall{.from = 4, .to = 5, .t = tf},
                      Wall{.from = 1, .to = 4, .t = tw}}};
}

} // namespace

// REQ-008b-02: A, the centroid, the inertias and the principal axes are
// exact.
TEST_CASE("geometry of solid polygons", "[core][section][solid]")
{
    SECTION("a rectangle with a hole, and two regions (Steiner)")
    {
        const SectionProperties p =
            of(solid(rectangle(0, 0, 0.4, 0.2), {rectangle(0.1, 0.05, 0.3, 0.15)}));
        CHECK(near(p.area, 0.08 - 0.02, 1e-12));
        CHECK(near(p.inertiaZ, (0.2 * std::pow(0.4, 3) - 0.1 * std::pow(0.2, 3)) / 12, 1e-10));
        CHECK(near(p.inertiaY, (0.4 * std::pow(0.2, 3) - 0.2 * std::pow(0.1, 3)) / 12, 1e-10));
        CHECK((p.centroid - Vec(0.2, 0.1)).norm() <= 1e-12);

        const SolidPolygon two{.regions = {{.outline = rectangle(-1.0, 0, -0.8, 0.1), .holes = {}},
                                           {.outline = rectangle(0.8, 0, 1.0, 0.1), .holes = {}}}};
        const SectionProperties q = of(two);
        CHECK(near(q.inertiaZ, 2 * (0.1 * std::pow(0.2, 3) / 12 + 0.02 * 0.81), 1e-10));
    }
    SECTION("an L equal to the parametric angle, principal axes included")
    {
        const fem::core::Angle angle{.b = 0.1, .h = 0.15, .t = 0.01};
        const Ring l = {Vec(0, 0),       Vec(0.15, 0),   Vec(0.15, 0.01),
                        Vec(0.01, 0.01), Vec(0.01, 0.1), Vec(0, 0.1)};
        const SectionProperties p = of(solid(l));
        const SectionProperties q = of(angle);
        CHECK(near(p.area, q.area, 1e-12));
        CHECK(near(p.inertiaY, q.inertiaY, 1e-10));
        CHECK(near(p.inertiaZ, q.inertiaZ, 1e-10));
        CHECK(near(p.principalAngle, q.principalAngle, 1e-10));
        CHECK((p.centroid - q.centroid).norm() <= 1e-12);
    }
}

// REQ-008b-02, NFR-008b-02: J, the shear areas, the shear centre and Iw from
// the finite elements, within 0.5% with the default mesh.
TEST_CASE("torsion and shear of solid polygons", "[core][section][solid]")
{
    SECTION("rectangle: Saint-Venant's series and 5/6")
    {
        const SectionProperties p = of(solid(rectangle(-0.25, -0.1, 0.25, 0.1)));
        const SectionProperties series = of(fem::core::Rectangle{.b = 0.2, .h = 0.5});
        CHECK(near(p.torsionConstant, series.torsionConstant, 0.005));
        CHECK(near(p.shearAreaY.value_or(0.0), 5.0 / 6.0 * p.area, 0.005));
        CHECK(near(p.shearAreaZ.value_or(0.0), 5.0 / 6.0 * p.area, 0.005));
        CHECK(p.shearCentre.norm() <= 1e-8);
    }
    SECTION("thin rectangle: Vlasov's Iw of a plate")
    {
        const SectionProperties p = of(solid(rectangle(-0.2, -0.005, 0.2, 0.005)));
        CHECK(near(p.warpingConstant, std::pow(0.4 * 0.01, 3) / 144, 0.05));
    }
    SECTION("circle and tube: exact J, 6/7 of Cowper with nu = 0")
    {
        const double r = 0.05;
        const SectionProperties disc = of(solid(circle(r, 128)));
        CHECK(near(disc.torsionConstant, kPi * std::pow(r, 4) / 2, 0.005));
        CHECK(near(disc.shearAreaY.value_or(0.0), 6.0 / 7.0 * disc.area, 0.005));
        CHECK(disc.warpingConstant <= 1e-10 * disc.torsionConstant * r * r);
        const SectionProperties tube = of(solid(circle(r, 128), {circle(0.8 * r, 128)}));
        CHECK(near(tube.torsionConstant, kPi * (std::pow(r, 4) - std::pow(0.8 * r, 4)) / 2, 0.005));
    }
    SECTION("I and channel against the thin-walled sections")
    {
        const double h = 0.3;
        const double bf = 0.15;
        const double tf = 0.0107;
        const double tw = 0.0071;
        const Ring ipe = {
            Vec(-h / 2, -bf / 2),     Vec(-h / 2 + tf, -bf / 2), Vec(-h / 2 + tf, -tw / 2),
            Vec(h / 2 - tf, -tw / 2), Vec(h / 2 - tf, -bf / 2),  Vec(h / 2, -bf / 2),
            Vec(h / 2, bf / 2),       Vec(h / 2 - tf, bf / 2),   Vec(h / 2 - tf, tw / 2),
            Vec(-h / 2 + tf, tw / 2), Vec(-h / 2 + tf, bf / 2),  Vec(-h / 2, bf / 2)};
        const SectionProperties i = of(solid(ipe));
        CHECK(near(i.warpingConstant, of(ipeMidLine(h, bf, tf, tw)).warpingConstant, 0.01));
        CHECK(i.shearCentre.norm() <= 1e-6 * h);

        const Ring u = {Vec(-h / 2, 0),       Vec(-h / 2, bf),     Vec(-h / 2 + tf, bf),
                        Vec(-h / 2 + tf, tw), Vec(h / 2 - tf, tw), Vec(h / 2 - tf, bf),
                        Vec(h / 2, bf),       Vec(h / 2, 0)};
        const SectionProperties c = of(solid(u));
        const SectionProperties thin = of(fem::core::Channel{.h = h, .bf = bf, .tf = tf, .tw = tw});
        CHECK(near(shearCentre(c).y(), shearCentre(thin).y(), 0.01));
        CHECK(std::abs(shearCentre(c).x()) <= 1e-6 * h);
        CHECK(near(c.warpingConstant, thin.warpingConstant, 0.02));
    }
}

// REQ-008b-02: J converges with h^4 on six-node triangles (design 008b
// §5.1.3).
TEST_CASE("the torsion constant converges with the mesh", "[core][section][solid]")
{
    const double exact = of(fem::core::Rectangle{.b = 0.1, .h = 0.3}).torsionConstant;
    std::vector<double> errors;
    for (const double size : {0.1, 0.05, 0.025}) {
        const SectionProperties p = of(solid(rectangle(-0.15, -0.05, 0.15, 0.05), {}, size));
        errors.push_back(std::abs(p.torsionConstant - exact) / exact);
    }
    const double rate = std::log2(errors[1] / errors[2]);
    INFO("errors " << errors[0] << " " << errors[1] << " " << errors[2]);
    CHECK(errors[0] > errors[1]);
    CHECK(rate >= 3.5);
    CHECK(rate <= 4.5);
}

// REQ-008b-02, 05: the solution keeps the fields of a solid polygon only.
TEST_CASE("solved sections", "[core][section][solid]")
{
    const auto polygon =
        fem::core::solveSection({.name = "P", .shape = solid(rectangle(-0.25, -0.1, 0.25, 0.1))});
    REQUIRE(polygon.has_value());
    CHECK((*polygon)->field() != nullptr);
    CHECK(near((*polygon)->properties().area, 0.1, 1e-12));
    const auto shape =
        fem::core::solveSection({.name = "R", .shape = fem::core::Rectangle{.b = 0.2, .h = 0.5}});
    REQUIRE(shape.has_value());
    CHECK((*shape)->field() == nullptr);
    CHECK_FALSE((*shape)->points().empty());
    const auto bad = fem::core::solveSection(
        {.name = "B",
         .shape = SolidPolygon{.regions = {{.outline = {Vec(0, 0), Vec(1, 0)}, .holes = {}}}}});
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().message ==
          "Section \"B\": the outline of the region 0 has fewer than three vertices.");
}

// REQ-008b-06: the stresses of a solid polygon from its fields.
TEST_CASE("stresses of solid polygons", "[core][section][solid]")
{
    const double h = 0.5;
    const double b = 0.2;
    const auto solved = fem::core::solveSection(
        {.name = "R", .shape = solid(rectangle(-h / 2, -b / 2, h / 2, b / 2))});
    REQUIRE(solved.has_value());
    const fem::core::SectionSolution& solution = **solved;
    const SectionProperties& p = solution.properties();

    SECTION("torsion: the largest shear at the middle of the long sides")
    {
        const fem::core::BeamForces torque{.t = 100.0};
        const fem::core::PointStress largest = fem::core::largestVonMises(solution, torque);
        const auto points = fem::core::stressPoints(fem::core::Rectangle{.b = b, .h = h});
        const auto side = std::ranges::find_if(points, [](const fem::core::StressPoint& point) {
            return point.name == "middle of the side +z";
        });
        REQUIRE(side != points.end());
        CHECK(near(largest.shear, 100.0 * side->torsionFactor, 0.01));
        CHECK(std::abs(largest.position.x()) <= 0.01 * h);
        CHECK(near(std::abs(largest.position.y()), b / 2, 1e-12));
    }
    SECTION("shear force: 3V / 2A at the centroid")
    {
        const fem::core::BeamForces shear{.vy = 1000.0};
        CHECK(near(fem::core::stressAt(solution, shear, Vec::Zero()).shear, 1.5 * 1000.0 / p.area,
                   0.01));
        CHECK(fem::core::stressAt(solution, shear, Vec(h / 2, 0.0)).shear <=
              0.01 * 1500.0 / p.area);
    }
    SECTION("normal stress at the vertices, exact")
    {
        const fem::core::BeamForces forces{.n = 5e3, .my = 2e2, .mz = -3e2};
        const auto stresses = fem::core::sectionStresses(solution, forces);
        REQUIRE(stresses.size() == 4);
        for (const fem::core::PointStress& s : stresses) {
            const double expected = 5e3 / p.area - (-3e2) * s.position.x() / p.inertiaZ +
                                    2e2 * s.position.y() / p.inertiaY;
            CHECK(near(s.normal, expected, 1e-10));
            CHECK(near(std::abs(s.position.x()), h / 2, 1e-12));
        }
        CHECK(stresses.front().name == "vertex 0 of the outline of the region 0");
    }
}

// REQ-008d-12: the cubic moment of a solid polygon is exact: a tee drawn as
// a polygon (and as a rectangle with a notch removed as two holes would
// not be simpler) gives the integral of y (y^2 + z^2) of the parametric
// tee, beta_z + 2 ys times Iz (their shear centres differ: numerical
// against the mid-lines).
TEST_CASE("monosymmetry of a solid polygon", "[core][section][solid]")
{
    const double h = 0.3;
    const double bf = 0.2;
    const double tf = 0.02;
    const double tw = 0.012;
    const SectionProperties parametric = of(fem::core::Tee{.h = h, .bf = bf, .tf = tf, .tw = tw});
    const Ring tee = {Vec(0.0, -tw / 2),   Vec(h - tf, -tw / 2), Vec(h - tf, -bf / 2),
                      Vec(h, -bf / 2),     Vec(h, bf / 2),       Vec(h - tf, bf / 2),
                      Vec(h - tf, tw / 2), Vec(0.0, tw / 2)};
    const SectionProperties polygon = of(solid(tee));
    const auto cubic = [](const SectionProperties& p) {
        return (p.monosymmetryZ + 2.0 * p.shearCentre.x()) * p.inertiaZ;
    };
    CHECK(near(cubic(polygon), cubic(parametric), 1e-10));
    // -2 zs with the numerical shear centre (zero by symmetry).
    CHECK(std::abs(polygon.monosymmetryY) <= 1e-6 * h);
    CHECK(near(polygon.polarRadius2,
               (polygon.inertiaY + polygon.inertiaZ) / polygon.area +
                   polygon.shearCentre.squaredNorm(),
               1e-12));
}
