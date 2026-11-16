// REQ-008-05, 16 to 19: loads of the span and their equivalent nodal loads.

#include <fem/core/beam_loads.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Core>
#include <array>
#include <cmath>
#include <numbers>

using fem::core::Axes;
using fem::core::BeamData;
using fem::core::BeamVector;
using fem::core::SpanFrame;
using fem::core::SpanLoads;

namespace {

constexpr double kL = 4.0;

BeamData beam(bool timoshenko)
{
    BeamData b;
    b.axial = 2e6;
    b.bendingY = 3e4;
    b.bendingZ = 5e4;
    b.torsional = 1e3;
    if (timoshenko) {
        b.shearY = 8e4;
        b.shearZ = 6e4;
    }
    return b;
}

bool near(double value, double reference, double scale)
{
    return std::abs(value - reference) <= 1e-12 * scale;
}

// Fixed-end loads of a point force P at a on an Euler-Bernoulli beam
// (any table, e.g. Roark table 8.1): the nodal loads are minus the reactions.
BeamVector pointForceY(double p, double a)
{
    const double b = kL - a;
    BeamVector f = BeamVector::Zero();
    f(1) = p * b * b * (3 * a + b) / std::pow(kL, 3);
    f(5) = p * a * b * b / (kL * kL);
    f(7) = p * a * a * (a + 3 * b) / std::pow(kL, 3);
    f(11) = -p * a * a * b / (kL * kL);
    return f;
}

} // namespace

TEST_CASE("positions along the bar", "[core][beam][loads]")
{
    CHECK(fem::core::positionOn({.value = 0.25, .fraction = true}, 8.0) == 2.0);
    CHECK(fem::core::positionOn({.value = 3.0, .fraction = false}, 8.0) == 3.0);
}

// REQ-008-16, 19: distributed loads against the fixed-end tables.
TEST_CASE("equivalent loads of distributed loads", "[core][beam][loads]")
{
    const double q = 3.0;
    const SpanFrame frame; // principal axes = global
    SECTION("uniform, both theories: qL/2 and qL^2/12")
    {
        const bool timoshenko = GENERATE(false, true);
        SpanLoads loads;
        fem::core::addDistributed(loads, frame, Axes::Local, Eigen::Vector3d(1.0, q, -2.0 * q),
                                  Eigen::Vector3d(1.0, q, -2.0 * q), 0.0, kL);
        const BeamVector f = fem::core::equivalentLoads(beam(timoshenko), kL, loads);
        const double s = q * kL * kL;
        CHECK(near(f(0), kL / 2, s));
        CHECK(near(f(6), kL / 2, s));
        CHECK(near(f(1), q * kL / 2, s));
        CHECK(near(f(5), q * kL * kL / 12, s));
        CHECK(near(f(7), q * kL / 2, s));
        CHECK(near(f(11), -q * kL * kL / 12, s));
        // x-z: qz = -2q; ry = -dw/dx flips the moments.
        CHECK(near(f(2), -q * kL, s));
        CHECK(near(f(4), 2 * q * kL * kL / 12, s));
        CHECK(near(f(10), -2 * q * kL * kL / 12, s));
        CHECK(f(3) == 0.0);
    }
    SECTION("triangular, Euler-Bernoulli: 3qL/20, qL^2/30, 7qL/20, qL^2/20")
    {
        SpanLoads loads;
        fem::core::addDistributed(loads, frame, Axes::Global, Eigen::Vector3d::Zero(),
                                  Eigen::Vector3d(0.0, q, 0.0), 0.0, kL);
        const BeamVector f = fem::core::equivalentLoads(beam(false), kL, loads);
        const double s = q * kL * kL;
        CHECK(near(f(1), 3 * q * kL / 20, s));
        CHECK(near(f(5), q * kL * kL / 30, s));
        CHECK(near(f(7), 7 * q * kL / 20, s));
        CHECK(near(f(11), -q * kL * kL / 20, s));
    }
    SECTION("partial trapezoidal: the integral of the point-force table")
    {
        const double a = 1.0;
        const double b = 2.5;
        SpanLoads loads;
        fem::core::addDistributed(loads, frame, Axes::Local, Eigen::Vector3d(0.0, 1.0, 0.0),
                                  Eigen::Vector3d(0.0, 4.0, 0.0), a, b);
        const BeamVector f = fem::core::equivalentLoads(beam(false), kL, loads);
        // Simpson on 2000 intervals of the table: exact for this quintic.
        BeamVector expected = BeamVector::Zero();
        const int n = 2000;
        for (int i = 0; i <= n; ++i) {
            const double t = static_cast<double>(i) / n;
            const double w = (i == 0 || i == n) ? 1.0 : (i % 2 == 1 ? 4.0 : 2.0);
            expected += w * (b - a) / (3.0 * n) * pointForceY(1.0 + 3.0 * t, a + t * (b - a));
        }
        CHECK((f - expected).norm() <= 1e-12 * expected.norm());
    }
}

// REQ-008-16, 19: concentrated forces and moments.
TEST_CASE("equivalent loads of concentrated loads", "[core][beam][loads]")
{
    const SpanFrame frame;
    SECTION("force, Euler-Bernoulli")
    {
        SpanLoads loads;
        fem::core::addConcentrated(loads, frame, Axes::Local, Eigen::Vector3d(0.0, 5.0, 0.0),
                                   Eigen::Vector3d::Zero(), 1.0);
        const BeamVector f = fem::core::equivalentLoads(beam(false), kL, loads);
        CHECK((f - pointForceY(5.0, 1.0)).norm() <= 1e-12 * 5.0 * kL);
    }
    SECTION("force at mid-span, Timoshenko: PL/8 by symmetry")
    {
        SpanLoads loads;
        fem::core::addConcentrated(loads, frame, Axes::Local, Eigen::Vector3d(0.0, 0.0, 8.0),
                                   Eigen::Vector3d::Zero(), kL / 2);
        const BeamVector f = fem::core::equivalentLoads(beam(true), kL, loads);
        CHECK(near(f(2), 4.0, 8.0 * kL));
        CHECK(near(f(8), 4.0, 8.0 * kL));
        CHECK(near(f(4), -8.0 * kL / 8, 8.0 * kL));
        CHECK(near(f(10), 8.0 * kL / 8, 8.0 * kL));
    }
    SECTION("moment, Euler-Bernoulli: -6Mab/L^3 and M b(b - 2a)/L^2")
    {
        const double m = 7.0;
        const double a = 1.0;
        const double b = kL - a;
        SpanLoads loads;
        fem::core::addConcentrated(loads, frame, Axes::Local, Eigen::Vector3d::Zero(),
                                   Eigen::Vector3d(2.0, 0.0, m), a);
        const BeamVector f = fem::core::equivalentLoads(beam(false), kL, loads);
        CHECK(near(f(1), -6 * m * a * b / std::pow(kL, 3), m));
        CHECK(near(f(5), m * b * (b - 2 * a) / (kL * kL), m));
        CHECK(near(f(7), 6 * m * a * b / std::pow(kL, 3), m));
        CHECK(near(f(11), m * a * (a - 2 * b) / (kL * kL), m));
        CHECK(near(f(3), 2.0 * b / kL, m));
        CHECK(near(f(9), 2.0 * a / kL, m));
    }
}

// REQ-008-16, 18, 19: imposed strain and curvature.
TEST_CASE("equivalent loads of temperature", "[core][beam][loads]")
{
    const SpanFrame frame;
    SpanLoads loads;
    loads.imposedStrain = 1e-4;
    fem::core::addGradient(loads, frame, 1e-5, 20.0, -10.0);
    CHECK(loads.curvatureZ == -1e-5 * 20.0);
    CHECK(loads.curvatureY == 1e-5 * -10.0);
    const BeamData b = beam(true);
    const BeamVector f = fem::core::equivalentLoads(b, kL, loads);
    CHECK(near(f(0), -b.axial * 1e-4, 1.0));
    CHECK(near(f(6), b.axial * 1e-4, 1.0));
    CHECK(near(f(5), -b.bendingZ * loads.curvatureZ, 1.0));
    CHECK(near(f(11), b.bendingZ * loads.curvatureZ, 1.0));
    CHECK(near(f(4), -b.bendingY * loads.curvatureY, 1.0));
    CHECK(near(f(1), 0.0, 1.0));
}

// REQ-008-05, 16: global loads in local axes; the lever about the shear
// centre; the principal angle.
TEST_CASE("span loads in the principal axes", "[core][beam][loads]")
{
    SpanFrame frame;
    // A bar along global Y with y' = -X and z' = Z.
    frame.principalAxes << 0, -1, 0, //
        1, 0, 0,                     //
        0, 0, 1;
    frame.shearCentre = Eigen::Vector2d(0.0, -0.05); // ez
    SpanLoads loads;
    fem::core::addDistributed(loads, frame, Axes::Global, Eigen::Vector3d(2.0, 0.0, 0.0),
                              Eigen::Vector3d(2.0, 0.0, 0.0), 0.0, kL);
    REQUIRE(loads.pieces.size() == 1);
    const auto& piece = loads.pieces.front();
    CHECK((piece.start.head<3>() - Eigen::Vector3d(0.0, -2.0, 0.0)).norm() <= 1e-15);
    // (C - S) x q about x: ez qy = -0.05 * -2.
    CHECK(std::abs(piece.start(3) - 0.1) <= 1e-15);

    // A local load turns by the principal angle.
    SpanFrame turned;
    turned.principalAngle = std::numbers::pi / 2;
    SpanLoads local;
    fem::core::addConcentrated(local, turned, Axes::Local, Eigen::Vector3d(0.0, 1.0, 0.0),
                               Eigen::Vector3d::Zero(), 1.0);
    CHECK((local.points.front().force - Eigen::Vector3d(0.0, 0.0, -1.0)).norm() <= 1e-15);

    // On the centroid dofs, a uniform load through the centroid gives no
    // nodal torque: the twist comes from the coupling in the stiffness.
    BeamData b = beam(false);
    b.shearCentre = frame.shearCentre;
    const BeamVector f = fem::core::equivalentLoads(b, kL, loads);
    CHECK(std::abs(f(3)) <= 1e-15);
    CHECK(std::abs(f(9)) <= 1e-15);
}

// REQ-008-19: the connections and the offsets act on the equivalent loads.
TEST_CASE("equivalent loads through connections and offsets", "[core][beam][loads]")
{
    const double q = 2.0;
    BeamData b = beam(false);
    SpanLoads loads;
    fem::core::addDistributed(loads, SpanFrame{}, Axes::Local, Eigen::Vector3d(0.0, q, 0.0),
                              Eigen::Vector3d(0.0, q, 0.0), 0.0, kL);
    b.fixedEndLoads = fem::core::equivalentLoads(b, kL, loads);

    const auto nodal = [](const BeamData& data, const Eigen::Vector3d& x1,
                          const Eigen::Vector3d& x2) {
        const std::array<BeamData, 1> beams = {data};
        const fem::core::BeamElement element(fem::core::ElementKind::Beam3D, beams);
        fem::core::ElementNodes nodes;
        nodes.type = fem::core::ElementType::Line2;
        nodes.count = 2;
        nodes.x[0] = x1;
        nodes.x[1] = x2;
        Eigen::MatrixXd k = Eigen::MatrixXd::Zero(12, 12);
        Eigen::VectorXd f = Eigen::VectorXd::Zero(12);
        REQUIRE(element.compute(nodes, 0, k, f).has_value());
        return f;
    };

    // Fixed-pinned: 5qL/8, qL^2/8 at the fixed end, 3qL/8 and no moment at
    // the pin.
    BeamData pinned = b;
    pinned.springs[1][5] = 0.0;
    const Eigen::VectorXd f = nodal(pinned, Eigen::Vector3d::Zero(), Eigen::Vector3d(kL, 0, 0));
    const double s = q * kL * kL;
    CHECK(near(f(1), 5 * q * kL / 8, s));
    CHECK(near(f(5), q * kL * kL / 8, s));
    CHECK(near(f(7), 3 * q * kL / 8, s));
    CHECK(near(f(11), 0.0, s));

    // Offsets carry the forces to the nodes with their moment r x F.
    BeamData offset = b;
    const double c = 0.3;
    offset.offsets = {Eigen::Vector3d(0, 0, c), Eigen::Vector3d(0, 0, c)};
    const Eigen::VectorXd g = nodal(offset, Eigen::Vector3d(0, 0, -c), Eigen::Vector3d(kL, 0, -c));
    CHECK(near(g(1), q * kL / 2, s));
    CHECK(near(g(3), -c * q * kL / 2, s));
    CHECK(near(g(5), q * kL * kL / 12, s));
}
