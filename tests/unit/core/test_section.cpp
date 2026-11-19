// REQ-008-02 to 05: section properties of the generic section and of the
// eight shapes.

#include <fem/core/beam_result.hpp>
#include <fem/core/section.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/LU>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using fem::core::Section;
using fem::core::SectionProperties;
using fem::core::SectionShape;

namespace {

constexpr double kPi = std::numbers::pi;

bool near(double value, double reference, double tolerance)
{
    return std::abs(value - reference) <= tolerance * std::max(1.0e-300, std::abs(reference));
}

SectionProperties of(const SectionShape& shape)
{
    Section section{.name = "S"};
    section.shape = shape;
    const auto properties = fem::core::sectionProperties(section);
    REQUIRE(properties.has_value());
    return properties.value_or(SectionProperties{});
}

std::string failure(const SectionShape& shape)
{
    Section section{.name = "S"};
    section.shape = shape;
    const auto properties = fem::core::sectionProperties(section);
    REQUIRE_FALSE(properties.has_value());
    return properties.error().message;
}

} // namespace

// REQ-008-03: the doubly symmetric shapes against closed forms.
TEST_CASE("properties of the symmetric shapes", "[core][section]")
{
    SECTION("rectangle")
    {
        const SectionProperties p = of(fem::core::Rectangle{.b = 0.2, .h = 0.5});
        CHECK(near(p.area, 0.1, 1e-12));
        CHECK(near(p.inertiaZ, 0.2 * std::pow(0.5, 3) / 12, 1e-12));
        CHECK(near(p.inertiaY, 0.5 * std::pow(0.2, 3) / 12, 1e-12));
        CHECK(p.principalAngle == 0.0);
        CHECK(p.centroid.norm() <= 1e-15);
        CHECK(p.shearCentre.norm() <= 1e-15);
        REQUIRE(p.shearAreaY.has_value());
        CHECK(near(p.shearAreaY.value_or(0.0), 5.0 / 6.0 * 0.1, 1e-12));
    }
    SECTION("rectangle torsion against tables")
    {
        // Square: J = 0.1406 a^4 (Timoshenko and Goodier, table of k1).
        CHECK(near(of(fem::core::Rectangle{.b = 1.0, .h = 1.0}).torsionConstant, 0.1406, 2e-4));
        // Thin strip: J -> a c^3 / 3 (1 - 0.630 c / a).
        CHECK(near(of(fem::core::Rectangle{.b = 1.0, .h = 100.0}).torsionConstant,
                   100.0 / 3.0 * (1.0 - 0.630 / 100.0), 1e-5));
        // The same J whichever side is longer.
        CHECK(near(of(fem::core::Rectangle{.b = 0.3, .h = 0.1}).torsionConstant,
                   of(fem::core::Rectangle{.b = 0.1, .h = 0.3}).torsionConstant, 1e-14));
    }
    SECTION("circle and circular tube, exact")
    {
        const SectionProperties c = of(fem::core::Circle{.d = 0.1});
        CHECK(near(c.area, kPi * 0.01 / 4, 1e-14));
        CHECK(near(c.inertiaY, kPi * 1e-4 / 64, 1e-14));
        CHECK(near(c.inertiaZ, c.inertiaY, 1e-14));
        CHECK(near(c.torsionConstant, kPi * 1e-4 / 32, 1e-14));

        const SectionProperties t = of(fem::core::CircularTube{.d = 0.2, .t = 0.01});
        const double inner = 0.18;
        CHECK(near(t.area, kPi * (0.04 - inner * inner) / 4, 1e-14));
        CHECK(near(t.inertiaZ, kPi * (std::pow(0.2, 4) - std::pow(inner, 4)) / 64, 1e-14));
        CHECK(near(t.torsionConstant, kPi * (std::pow(0.2, 4) - std::pow(inner, 4)) / 32, 1e-14));
        CHECK(near(t.shearAreaZ.value_or(0.0), 0.5 * t.area, 1e-14));
    }
    SECTION("rectangular tube")
    {
        const double b = 0.2;
        const double h = 0.3;
        const double t = 0.01;
        const SectionProperties p = of(fem::core::RectangularTube{.b = b, .h = h, .t = t});
        CHECK(near(p.area, b * h - (b - 2 * t) * (h - 2 * t), 1e-12));
        CHECK(near(p.inertiaZ, (b * h * h * h - (b - 2 * t) * std::pow(h - 2 * t, 3)) / 12, 1e-12));
        CHECK(near(p.inertiaY, (h * b * b * b - (h - 2 * t) * std::pow(b - 2 * t, 3)) / 12, 1e-12));
        // Bredt: 4 Am^2 t / pm on the mid-line.
        const double am = (b - t) * (h - t);
        CHECK(near(p.torsionConstant, 4 * am * am * t / (2 * ((b - t) + (h - t))), 1e-12));
        CHECK(near(p.shearAreaY.value_or(0.0), 2 * (h - t) * t, 1e-12));
    }
    SECTION("I section against the formula and the IPE 300 table")
    {
        const double h = 0.3;
        const double bf = 0.15;
        const double tf = 0.0107;
        const double tw = 0.0071;
        const SectionProperties p = of(fem::core::ISection{.h = h, .bf = bf, .tf = tf, .tw = tw});
        CHECK(near(p.area, 2 * bf * tf + (h - 2 * tf) * tw, 1e-12));
        CHECK(near(p.inertiaZ, (bf * h * h * h - (bf - tw) * std::pow(h - 2 * tf, 3)) / 12, 1e-12));
        CHECK(near(p.inertiaY, (2 * tf * std::pow(bf, 3) + (h - 2 * tf) * std::pow(tw, 3)) / 12,
                   1e-12));
        CHECK(near(p.torsionConstant,
                   (2 * bf * std::pow(tf, 3) + (h - 2 * tf) * std::pow(tw, 3)) / 3, 1e-12));
        CHECK(p.shearCentre.norm() <= 1e-15);
        // The rolled IPE 300 has root fillets, which the plates leave out:
        // A 53.8 cm2, Iz 8356 cm4, Iy 604 cm4, J 20.1 cm4 (design 008 §5.1).
        CHECK(near(p.area, 53.8e-4, 0.05));
        CHECK(near(p.inertiaZ, 8356e-8, 0.05));
        CHECK(near(p.inertiaY, 604e-8, 0.05));
        CHECK(near(p.torsionConstant, 20.1e-8, 0.25));
    }
}

// REQ-008-03, 05: shapes with a shear centre away from the centroid.
TEST_CASE("properties of the asymmetric shapes", "[core][section]")
{
    SECTION("tee")
    {
        const double h = 0.2;
        const double bf = 0.15;
        const double tf = 0.02;
        const double tw = 0.01;
        const SectionProperties p = of(fem::core::Tee{.h = h, .bf = bf, .tf = tf, .tw = tw});
        const double af = bf * tf;
        const double aw = (h - tf) * tw;
        const double yc = (af * (h - tf / 2) + aw * (h - tf) / 2) / (af + aw);
        CHECK(near(p.area, af + aw, 1e-12));
        CHECK(near(p.centroid.x(), yc, 1e-12));
        CHECK(std::abs(p.centroid.y()) <= 1e-15);
        const double iz = bf * std::pow(tf, 3) / 12 + af * std::pow(h - tf / 2 - yc, 2) +
                          tw * std::pow(h - tf, 3) / 12 + aw * std::pow((h - tf) / 2 - yc, 2);
        CHECK(near(p.inertiaZ, iz, 1e-12));
        CHECK(near(p.inertiaY, tf * std::pow(bf, 3) / 12 + (h - tf) * std::pow(tw, 3) / 12, 1e-12));
        CHECK(p.principalAngle == 0.0);
        // At the meeting of the mid-lines.
        CHECK(near(p.shearCentre.x(), h - tf / 2 - yc, 1e-12));
        CHECK(std::abs(p.shearCentre.y()) <= 1e-15);
    }
    SECTION("channel")
    {
        const double h = 0.2;
        const double bf = 0.075;
        const double tf = 0.0115;
        const double tw = 0.0085;
        const SectionProperties p = of(fem::core::Channel{.h = h, .bf = bf, .tf = tf, .tw = tw});
        const double aw = h * tw;
        const double af = (bf - tw) * tf;
        const double zc = (aw * tw / 2 + 2 * af * (tw + bf) / 2) / (aw + 2 * af);
        CHECK(near(p.area, aw + 2 * af, 1e-12));
        CHECK(near(p.centroid.y(), zc, 1e-12));
        CHECK(std::abs(p.centroid.x()) <= 1e-15);
        CHECK(p.principalAngle == 0.0);
        // e = 3 b^2 tf / (6 b tf + h tw) behind the web mid-line.
        const double b = bf - tw / 2;
        const double e = 3 * b * b * tf / (6 * b * tf + (h - tf) * tw);
        CHECK(near(p.shearCentre.y(), tw / 2 - e - zc, 1e-12));
        CHECK(std::abs(p.shearCentre.x()) <= 1e-15);
        CHECK(near(p.torsionConstant, (2 * (bf - tw) * std::pow(tf, 3) + h * std::pow(tw, 3)) / 3,
                   1e-12));
    }
    SECTION("equal angle: principal axes at 45 degrees")
    {
        const double a = 0.1;
        const double t = 0.01;
        const SectionProperties p = of(fem::core::Angle{.b = a, .h = a, .t = t});
        const double a1 = a * t;       // leg along y
        const double a2 = (a - t) * t; // leg along z
        const double c = (a1 * a / 2 + a2 * t / 2) / (a1 + a2);
        CHECK(near(p.centroid.x(), c, 1e-12));
        CHECK(near(p.centroid.y(), c, 1e-12));
        CHECK(near(std::abs(p.principalAngle), kPi / 4, 1e-12));
        // Invariants of the rotation.
        const double iz = t * a * a * a / 12 + a1 * std::pow(a / 2 - c, 2) +
                          (a - t) * t * t * t / 12 + a2 * std::pow(t / 2 - c, 2);
        const double iyz = a1 * (a / 2 - c) * (t / 2 - c) + a2 * (t / 2 - c) * ((a + t) / 2 - c);
        CHECK(near(p.inertiaY + p.inertiaZ, 2 * iz, 1e-12));
        CHECK(near(p.inertiaY * p.inertiaZ, iz * iz - iyz * iyz, 1e-12));
        // The shear centre at the corner of the mid-lines lies on the
        // symmetry axis, a principal axis.
        const Eigen::Vector2d corner(t / 2 - c, t / 2 - c);
        CHECK(near(p.shearCentre.norm(), corner.norm(), 1e-12));
        CHECK(std::min(std::abs(p.shearCentre.x()), std::abs(p.shearCentre.y())) <= 1e-14);
    }
    SECTION("unequal angle: principal inertias")
    {
        const SectionProperties p = of(fem::core::Angle{.b = 0.1, .h = 0.15, .t = 0.012});
        CHECK(std::abs(p.principalAngle) > 0.1);
        CHECK(std::abs(p.principalAngle) <= kPi / 4);
        // The larger inertia stays about z' (y' near the long leg).
        CHECK(p.inertiaZ > p.inertiaY);
    }
}

// REQ-008-02, 05: a generic section is taken to its principal axes.
TEST_CASE("a generic section is rotated to its principal axes", "[core][section]")
{
    Section section{.name = "G", .area = 1.0};
    section.inertiaY = 2.0;
    section.inertiaZ = 4.0;
    section.productOfInertia = 1.0;
    section.torsionConstant = 0.5;
    section.shearCentre = Eigen::Vector2d(0.1, 0.0);
    section.shearAreaY = 0.8;
    section.shearAreaZ = 0.8;
    const auto p = fem::core::sectionProperties(section).value_or(SectionProperties{});
    // tan 2 theta = 2 Iyz / (Iz - Iy) = 1.
    CHECK(near(p.principalAngle, kPi / 8, 1e-14));
    CHECK(near(p.inertiaZ, 3.0 + std::sqrt(2.0), 1e-14));
    CHECK(near(p.inertiaY, 3.0 - std::sqrt(2.0), 1e-14));
    CHECK(near(p.shearCentre.x(), 0.1 * std::cos(kPi / 8), 1e-14));
    CHECK(near(p.shearCentre.y(), -0.1 * std::sin(kPi / 8), 1e-14));
    CHECK(near(p.shearAreaY.value_or(0.0), 0.8, 1e-14));
    CHECK(p.torsionConstant == 0.5);

    // A truss section: the area alone, nothing else needed.
    const auto bar = fem::core::sectionProperties(Section{.name = "Rod", .area = 2.0});
    REQUIRE(bar.has_value());
    CHECK(bar.value_or(SectionProperties{}).area == 2.0);
    CHECK_FALSE(bar.value_or(SectionProperties{}).shearAreaY.has_value());
}

// REQ-008-04
TEST_CASE("impossible shapes are refused", "[core][section]")
{
    CHECK(failure(fem::core::Rectangle{.b = 0.0, .h = 1.0}) ==
          "Section \"S\": the dimension b must be positive.");
    CHECK(failure(fem::core::Circle{.d = -1.0}) ==
          "Section \"S\": the dimension d must be positive.");
    CHECK(failure(fem::core::CircularTube{.d = 0.1, .t = 0.05}) ==
          "Section \"S\": the wall t must be less than d / 2.");
    CHECK(failure(fem::core::RectangularTube{.b = 0.1, .h = 0.3, .t = 0.05}) ==
          "Section \"S\": the wall t must be less than b / 2 and h / 2.");
    CHECK(failure(fem::core::ISection{.h = 0.2, .bf = 0.1, .tf = 0.1, .tw = 0.01}) ==
          "Section \"S\": 2 tf must be less than h.");
    CHECK(failure(fem::core::ISection{.h = 0.2, .bf = 0.1, .tf = 0.01, .tw = 0.2}) ==
          "Section \"S\": tw must not exceed bf.");
    CHECK(failure(fem::core::Channel{.h = 0.2, .bf = 0.1, .tf = 0.01, .tw = 0.0}) ==
          "Section \"S\": the dimension tw must be positive.");
    CHECK(failure(fem::core::Tee{.h = 0.2, .bf = 0.1, .tf = 0.2, .tw = 0.01}) ==
          "Section \"S\": tf must be less than h.");
    CHECK(failure(fem::core::Angle{.b = 0.1, .h = 0.1, .t = 0.1}) ==
          "Section \"S\": the thickness t must be less than b and h.");
}

namespace {

// By value: a reference would trip GCC 13 -Wdangling-reference (the name is a
// temporary).
fem::core::PointStress at(const std::vector<fem::core::PointStress>& stresses,
                          const std::string& name)
{
    const auto found =
        std::ranges::find_if(stresses, [&name](const auto& stress) { return stress.name == name; });
    INFO("stress point " << name);
    REQUIRE(found != stresses.end());
    return *found;
}

} // namespace

// REQ-008-23: stresses at the points of the shapes.
TEST_CASE("stresses in the section", "[core][section]")
{
    const fem::core::BeamForces f{.n = 1e5, .vy = 3e4, .vz = -1e4, .t = 2e3, .my = -5e3, .mz = 2e4};
    SECTION("rectangle: bending, 3V/2A and torsion")
    {
        const double b = 0.2;
        const double h = 0.4;
        Section section{.name = "R"};
        section.shape = fem::core::Rectangle{.b = b, .h = h};
        const SectionProperties p = of(*section.shape);
        const auto stresses = fem::core::sectionStresses(section, f);
        const auto& corner = at(stresses, "corner +y +z");
        const double sigma =
            f.n / p.area - f.mz * (h / 2) / p.inertiaZ + f.my * (b / 2) / p.inertiaY;
        CHECK(near(corner.normal, sigma, 1e-12));
        CHECK(corner.shear == 0.0);
        CHECK(near(corner.vonMises, std::abs(sigma), 1e-12));
        const auto& centre = at(stresses, "centre");
        CHECK(near(centre.shear, 1.5 * (std::abs(f.vy) + std::abs(f.vz)) / p.area, 1e-12));
        CHECK(near(centre.normal, f.n / p.area, 1e-12));
        // h/b = 2: tau = T / (0.246 h b^2) (Timoshenko and Goodier, table of k2).
        const auto& side = at(stresses, "middle of the side +z");
        const double torsion = side.shear - 1.5 * std::abs(f.vy) / p.area;
        CHECK(near(torsion, f.t / (0.246 * h * b * b), 5e-3));
        CHECK(near(side.vonMises,
                   std::sqrt(side.normal * side.normal + 3 * side.shear * side.shear), 1e-12));
    }
    SECTION("I section: shear in the web")
    {
        const fem::core::ISection s{.h = 0.3, .bf = 0.15, .tf = 0.0107, .tw = 0.0071};
        const Section section{.name = "I", .shape = s};
        const SectionProperties p = of(s);
        const auto stresses = fem::core::sectionStresses(section, f);
        const double q = s.bf * s.tf * (s.h - s.tf) / 2 + s.tw * std::pow(s.h / 2 - s.tf, 2) / 2;
        CHECK(near(at(stresses, "web centre").shear,
                   std::abs(f.vy) * q / (p.inertiaZ * s.tw) +
                       std::abs(f.t) * s.tw / p.torsionConstant,
                   1e-12));
        // Close to V over the web area.
        CHECK(near(std::abs(f.vy) * q / (p.inertiaZ * s.tw),
                   std::abs(f.vy) / ((s.h - 2 * s.tf) * s.tw), 0.15));
        CHECK(near(at(stresses, "corner -y -z").normal,
                   f.n / p.area + f.mz * (s.h / 2) / p.inertiaZ - f.my * (s.bf / 2) / p.inertiaY,
                   1e-12));
    }
    SECTION("circular tube: T r / J and about 2V/A")
    {
        const fem::core::CircularTube s{.d = 0.2, .t = 0.005};
        const Section section{.name = "O", .shape = s};
        const SectionProperties p = of(s);
        const auto stresses = fem::core::sectionStresses(section, f);
        REQUIRE(stresses.size() == 8);
        const auto& side = at(stresses, "perimeter at 90 degrees"); // y = 0
        CHECK(near(side.shear - std::abs(f.t) * 0.1 / p.torsionConstant,
                   2 * std::abs(f.vy) / p.area, 0.02));
        CHECK(near(at(stresses, "perimeter at 0 degrees").normal,
                   f.n / p.area - f.mz * 0.1 / p.inertiaZ, 1e-12));
    }
    SECTION("channel: torsion T t / J at the flange tip")
    {
        const fem::core::Channel s{.h = 0.2, .bf = 0.075, .tf = 0.0115, .tw = 0.0085};
        const Section section{.name = "U", .shape = s};
        const SectionProperties p = of(s);
        const auto stresses =
            fem::core::sectionStresses(section, fem::core::BeamForces{.t = 300.0});
        CHECK(near(at(stresses, "flange tip +y").shear, 300.0 * s.tf / p.torsionConstant, 1e-12));
        CHECK(at(stresses, "web centre").normal == 0.0);
    }
    SECTION("generic section: unsymmetric bending in the drawing axes")
    {
        Section section{.name = "G", .area = 2e-2};
        section.inertiaY = 3e-5;
        section.inertiaZ = 9e-5;
        section.productOfInertia = 2e-5;
        const SectionProperties p =
            fem::core::sectionProperties(section).value_or(SectionProperties{});
        const fem::core::BeamForces bending{.n = 4e4, .my = 3e3, .mz = -6e3}; // principal axes
        // The moment vector in the drawing axes.
        const double c = std::cos(p.principalAngle);
        const double s = std::sin(p.principalAngle);
        const double myd = c * bending.my - s * bending.mz;
        const double mzd = s * bending.my + c * bending.mz;
        // sigma = a + b y + c z with int sigma y = -Mz and int sigma z = My.
        Eigen::Matrix2d inertia;
        inertia << 9e-5, 2e-5, 2e-5, 3e-5;
        const Eigen::Vector2d slope = inertia.inverse() * Eigen::Vector2d(-mzd, myd);
        const Eigen::Vector2d point(0.07, -0.04);
        CHECK(near(fem::core::normalStress(p, bending, point), bending.n / 2e-2 + slope.dot(point),
                   1e-12));
        CHECK(fem::core::sectionStresses(section, bending).empty());
    }
}

// REQ-008b-01: the warping constant and the sectorial coordinate of the
// shapes (design 008b §5.1.1), on the mid-lines.
TEST_CASE("warping constants and sectorial coordinates of the shapes", "[core][section]")
{
    const auto point = [](const SectionShape& shape, const std::string& name) {
        const auto points = fem::core::stressPoints(shape);
        const auto found = std::ranges::find_if(
            points, [&name](const fem::core::StressPoint& p) { return p.name == name; });
        REQUIRE(found != points.end());
        return *found;
    };

    SECTION("I: tf bf^3 h0^2 / 24, against the tables of the IPE 300")
    {
        const fem::core::ISection ipe{.h = 300.0, .bf = 150.0, .tf = 10.7, .tw = 7.1};
        const double h0 = 300.0 - 10.7;
        CHECK(near(of(ipe).warpingConstant, 10.7 * std::pow(150.0, 3) * h0 * h0 / 24, 1e-12));
        // 125.9e3 cm^6 in the tables, in mm^6.
        CHECK(near(of(ipe).warpingConstant, 125.9e9, 0.01));
        CHECK(near(point(ipe, "corner +y +z").sectorial, h0 * 150.0 / 4, 1e-12));
        CHECK(near(point(ipe, "corner +y -z").sectorial, -h0 * 150.0 / 4, 1e-12));
        CHECK(point(ipe, "web centre").sectorial == 0.0);
    }

    SECTION("channel: the closed form of Galambos")
    {
        const fem::core::Channel u{.h = 0.2, .bf = 0.075, .tf = 0.0115, .tw = 0.0085};
        const double b = u.bf - u.tw / 2;
        const double h0 = u.h - u.tf;
        const double galambos = u.tf * std::pow(b, 3) * h0 * h0 / 12 *
                                (3 * b * u.tf + 2 * h0 * u.tw) / (6 * b * u.tf + h0 * u.tw);
        CHECK(near(of(u).warpingConstant, galambos, 1e-12));
        const double e = 3 * b * b * u.tf / (6 * b * u.tf + h0 * u.tw);
        CHECK(near(point(u, "flange tip +y").sectorial, (b - e) * h0 / 2, 1e-12));
        CHECK(near(point(u, "web back +y").sectorial, -e * h0 / 2, 1e-12));
        CHECK(near(point(u, "flange tip -y").sectorial, -(b - e) * h0 / 2, 1e-12));
        CHECK(point(u, "web centre").sectorial == 0.0);
    }

    SECTION("rectangular tube: the closed cell")
    {
        const fem::core::RectangularTube box{.b = 0.3, .h = 0.2, .t = 0.01};
        const double b0 = 0.29;
        const double h0 = 0.19;
        CHECK(near(of(box).warpingConstant,
                   0.01 * b0 * b0 * h0 * h0 * std::pow(b0 - h0, 2) / (24 * (b0 + h0)), 1e-12));
        const double corner = b0 * h0 * (h0 - b0) / (4 * (b0 + h0));
        CHECK(near(point(box, "corner +y +z").sectorial, corner, 1e-12));
        CHECK(near(point(box, "corner -y +z").sectorial, -corner, 1e-12));
        // A square tube does not warp.
        CHECK(of(fem::core::RectangularTube{.b = 0.2, .h = 0.2, .t = 0.01}).warpingConstant == 0.0);
    }

    SECTION("rectangle: Vlasov's plate value")
    {
        CHECK(near(of(fem::core::Rectangle{.b = 0.01, .h = 0.3}).warpingConstant,
                   std::pow(0.01 * 0.3, 3) / 144, 1e-12));
    }

    SECTION("shapes that do not warp on their mid-lines")
    {
        CHECK(of(fem::core::Circle{.d = 0.1}).warpingConstant == 0.0);
        CHECK(of(fem::core::CircularTube{.d = 0.1, .t = 0.005}).warpingConstant == 0.0);
        CHECK(of(fem::core::Angle{.b = 0.1, .h = 0.15, .t = 0.01}).warpingConstant == 0.0);
        CHECK(of(fem::core::Tee{.h = 0.2, .bf = 0.15, .tf = 0.012, .tw = 0.008}).warpingConstant ==
              0.0);
    }

    SECTION("generic section: given, or zero")
    {
        Section generic{.name = "G", .area = 1.0, .inertiaY = 1.0, .inertiaZ = 1.0};
        CHECK(fem::core::sectionProperties(generic).value_or(SectionProperties{}).warpingConstant ==
              0.0);
        generic.warpingConstant = 2.5e-6;
        CHECK(fem::core::sectionProperties(generic).value_or(SectionProperties{}).warpingConstant ==
              2.5e-6);
    }
}

namespace {

// The integrals of y (y^2 + z^2) and z (y^2 + z^2) over the rectangle
// [y0, y1] x [z0, z1], exactly.
std::array<double, 2> cubicOfRectangle(double y0, double y1, double z0, double z1)
{
    const auto p = [](double a, double b, int n) { return (std::pow(b, n) - std::pow(a, n)) / n; };
    return {p(y0, y1, 4) * p(z0, z1, 1) + p(y0, y1, 2) * p(z0, z1, 3),
            p(y0, y1, 1) * p(z0, z1, 4) + p(y0, y1, 3) * p(z0, z1, 2)};
}

} // namespace

// REQ-008d-12: r0^2 and the monosymmetry coefficients beta_y and beta_z
// (design 008d §5.7), exact by the integrals of the rectangles of the
// shapes; zero for the doubly symmetric ones.
TEST_CASE("polar radius and monosymmetry of the shapes", "[core][section]")
{
    SECTION("doubly symmetric shapes: beta = 0")
    {
        for (const SectionShape& shape : std::vector<SectionShape>{
                 fem::core::Rectangle{.b = 0.2, .h = 0.5}, fem::core::Circle{.d = 0.3},
                 fem::core::RectangularTube{.b = 0.3, .h = 0.4, .t = 0.02},
                 fem::core::ISection{.h = 0.4, .bf = 0.2, .tf = 0.02, .tw = 0.01}}) {
            const SectionProperties p = of(shape);
            const double scale = std::sqrt(p.inertiaZ / p.area);
            CHECK(std::abs(p.monosymmetryY) <= 1e-12 * scale);
            CHECK(std::abs(p.monosymmetryZ) <= 1e-12 * scale);
            CHECK(near(p.polarRadius2, (p.inertiaY + p.inertiaZ) / p.area, 1e-12));
        }
    }
    SECTION("tee: beta_z from its two rectangles")
    {
        const double h = 0.3;
        const double bf = 0.2;
        const double tf = 0.02;
        const double tw = 0.012;
        const SectionProperties p = of(fem::core::Tee{.h = h, .bf = bf, .tf = tf, .tw = tw});
        const double yc = p.centroid.x();
        const auto flange = cubicOfRectangle(h - tf - yc, h - yc, -bf / 2, bf / 2);
        const auto stem = cubicOfRectangle(-yc, h - tf - yc, -tw / 2, tw / 2);
        const double ys = p.shearCentre.x();
        CHECK(near(p.monosymmetryZ, (flange[0] + stem[0]) / p.inertiaZ - 2.0 * ys, 1e-10));
        CHECK(std::abs(p.monosymmetryY) <= 1e-12 * h);
        CHECK(near(p.polarRadius2, (p.inertiaY + p.inertiaZ) / p.area + ys * ys, 1e-12));
    }
    SECTION("channel: beta_y from its three rectangles, beta_z = 0")
    {
        const double h = 0.3;
        const double bf = 0.1;
        const double tf = 0.015;
        const double tw = 0.01;
        const SectionProperties p = of(fem::core::Channel{.h = h, .bf = bf, .tf = tf, .tw = tw});
        const double zc = p.centroid.y();
        const auto web = cubicOfRectangle(-h / 2, h / 2, -zc, tw - zc);
        const auto top = cubicOfRectangle(h / 2 - tf, h / 2, tw - zc, bf - zc);
        const auto bottom = cubicOfRectangle(-h / 2, -h / 2 + tf, tw - zc, bf - zc);
        const double zs = p.shearCentre.y();
        CHECK(near(p.monosymmetryY, (web[1] + top[1] + bottom[1]) / p.inertiaY - 2.0 * zs, 1e-10));
        CHECK(std::abs(p.monosymmetryZ) <= 1e-12 * h);
    }
    SECTION("a generic section takes beta as given, or zero")
    {
        Section generic{.name = "G", .area = 1e-2};
        generic.inertiaY = 2e-5;
        generic.inertiaZ = 8e-5;
        generic.torsionConstant = 1e-6;
        generic.shearCentre = Eigen::Vector2d(0.03, 0.0);
        const auto plain = fem::core::sectionProperties(generic);
        REQUIRE(plain.has_value());
        CHECK(plain->monosymmetryY == 0.0);
        CHECK(plain->monosymmetryZ == 0.0);
        CHECK(near(plain->polarRadius2, (2e-5 + 8e-5) / 1e-2 + 0.03 * 0.03, 1e-12));
        generic.monosymmetryZ = 0.12;
        const auto given = fem::core::sectionProperties(generic);
        REQUIRE(given.has_value());
        CHECK(given->monosymmetryZ == 0.12);
    }
}
