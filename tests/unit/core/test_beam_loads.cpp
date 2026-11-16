// REQ-008-05, 15 to 19, 22: loads of the span, their equivalent nodal loads
// and the forces along the bar.

// GCC 13 at -O2 warns that the std::vector of the inactive NodeIds or
// ElementIds alternative "may be used uninitialized" when a temporary holding
// a GroupRef is moved into the model: a false positive of its variant
// analysis.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "beam_models.hpp"

#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_result.hpp>

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

namespace {

// A simply supported 2D beam of `pieces` bars, span kL, along x.
fem::core::Model simpleBeam(int pieces, fem::core::BeamTheory theory)
{
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= pieces; ++i) {
        spec.nodes.emplace_back(kL * i / pieces, 0.0, 0.0);
    }
    for (int i = 0; i < pieces; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model model =
        fem::core::test::beamModel(spec, fem::core::ElementKind::Beam2D, theory);
    fem::core::test::restrain(model, {1}, {fem::core::DofType::U, fem::core::DofType::V});
    fem::core::test::restrain(model, {static_cast<fem::core::ExternalId>(pieces + 1)},
                              {fem::core::DofType::V});
    return model;
}

} // namespace

// REQ-008-22: forces and deflections along the bar, exact for the span loads.
TEST_CASE("forces and displacements along a simply supported beam", "[core][beam][loads]")
{
    using fem::core::test::kE;
    using fem::core::test::kG;
    using fem::core::test::kIz;
    using fem::core::test::kShearArea;
    const bool timoshenko = GENERATE(false, true);
    const int pieces = GENERATE(1, 3);
    const double w = 5.0e3; // downwards
    fem::core::Model model = simpleBeam(pieces, timoshenko ? fem::core::BeamTheory::Timoshenko
                                                           : fem::core::BeamTheory::EulerBernoulli);
    model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1, 2, 3}},
                                      .startValue = Eigen::Vector3d(0.0, -w, 0.0),
                                      .endValue = Eigen::Vector3d(0.0, -w, 0.0)});
    if (pieces == 1) {
        model.distributedLoads.front().elements = fem::core::ElementIds{{1}};
    }
    const auto result = fem::core::test::solve(model);
    const double ei = kE * kIz;
    const double gas = timoshenko ? kG * kShearArea : 0.0;
    const double piece = kL / pieces;
    for (int e = 0; e < pieces; ++e) {
        const auto& beam = fem::core::test::beamOf(result, static_cast<std::size_t>(e));
        for (int k = 0; k <= 10; ++k) {
            const double s = piece * k / 10.0;
            const double x = e * piece + s;
            const fem::core::BeamPoint p = beam.at(s);
            const double m = w * x * (kL - x) / 2.0;
            const double v = -w * x * (kL * kL * kL - 2 * kL * x * x + x * x * x) / (24 * ei) -
                             (timoshenko ? w * x * (kL - x) / (2 * gas) : 0.0);
            const double rz = -w * (kL * kL * kL - 6 * kL * x * x + 4 * x * x * x) / (24 * ei);
            CHECK(fem::core::test::near(p.forces.mz, m, 1e-10, w * kL * kL));
            CHECK(fem::core::test::near(p.forces.vy, -w * kL / 2 + w * x, 1e-10, w * kL));
            CHECK(fem::core::test::near(p.displacement.y(), v, 1e-10, w * std::pow(kL, 4) / ei));
            CHECK(fem::core::test::near(p.rotation.z(), rz, 1e-10, w * std::pow(kL, 3) / ei));
        }
        // The ends of the evaluation are the end forces.
        CHECK(fem::core::test::near(beam.at(piece).forces.mz, beam.end().mz, 1e-12, w * kL * kL));
    }
}

// REQ-008-22: the jump under a concentrated load, on either side.
TEST_CASE("the jump under a concentrated load", "[core][beam][loads]")
{
    const double p = 8.0e3;
    const double a = 1.0;
    fem::core::Model model = simpleBeam(1, fem::core::BeamTheory::EulerBernoulli);
    model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                       .force = Eigen::Vector3d(0.0, -p, 0.0),
                                       .moment = Eigen::Vector3d(0.0, 0.0, 2.0e3),
                                       .at = {.value = a, .fraction = false}});
    const auto result = fem::core::test::solve(model);
    const auto& beam = fem::core::test::beamOf(result, 0);
    const auto before = beam.at(a, fem::core::Side::Before);
    const auto after = beam.at(a, fem::core::Side::After);
    CHECK(fem::core::test::near(after.forces.vy - before.forces.vy, p, 1e-12));
    CHECK(fem::core::test::near(before.forces.mz - after.forces.mz, 2.0e3, 1e-10));
    CHECK(fem::core::test::near(after.displacement.y(), before.displacement.y(), 1e-14, 1.0));
    // Reaction at the left: P b / L + M / L, up; Vy(0) is minus it.
    const double left = p * (kL - a) / kL + 2.0e3 / kL;
    CHECK(fem::core::test::near(beam.start().vy, -left, 1e-10, p));
    CHECK(fem::core::test::near(beam.at(0.0).forces.mz, 0.0, 1e-10, p * kL));
    CHECK(fem::core::test::near(beam.at(kL).forces.mz, 0.0, 1e-10, p * kL));
}

// REQ-008-15, 22: with a foundation the evaluation is approximate but
// consistent at the ends.
TEST_CASE("forces along a beam on an elastic foundation", "[core][beam][loads]")
{
    fem::core::Model model = simpleBeam(4, fem::core::BeamTheory::EulerBernoulli);
    model.foundations.push_back({.elements = fem::core::ElementIds{{1, 2, 3, 4}},
                                 .translational = Eigen::Vector3d(0, 1e7, 0)});
    model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1, 2, 3, 4}},
                                      .startValue = Eigen::Vector3d(0.0, -1e4, 0.0),
                                      .endValue = Eigen::Vector3d(0.0, -1e4, 0.0)});
    const auto result = fem::core::test::solve(model);
    for (std::size_t e = 0; e < 4; ++e) {
        const auto& beam = fem::core::test::beamOf(result, e);
        const auto end = beam.at(beam.length());
        CHECK(fem::core::test::near(end.forces.vy, beam.end().vy, 1e-6, 1e4 * kL));
        CHECK(fem::core::test::near(end.forces.mz, beam.end().mz, 1e-6, 1e4 * kL * kL));
    }
}

// REQ-008-16, 18, 19, NFR-008-01: temperature, initial strain and weight in
// analyses.
TEST_CASE("temperature and weight on beams", "[core][beam][loads]")
{
    using fem::core::test::kArea;
    using fem::core::test::kE;
    using fem::core::test::kIz;
    using fem::core::test::near;
    const double alpha = fem::core::test::steel().thermalExpansion.value_or(0.0);
    const double gy = 30.0; // dT/dy
    const auto row = [](int pieces) {
        fem::core::test::TrussSpec spec;
        for (int i = 0; i <= pieces; ++i) {
            spec.nodes.emplace_back(kL * i / pieces, 0.0, 0.0);
        }
        for (int i = 0; i < pieces; ++i) {
            spec.bars.push_back({i, i + 1});
        }
        return spec;
    };
    const fem::core::ElementIds all{{1, 2, 3}};

    SECTION("fixed-fixed with a gradient: M = EI alpha dT/dy, no displacement")
    {
        const bool timoshenko = GENERATE(false, true);
        fem::core::Model model = fem::core::test::beamModel(
            row(3), fem::core::ElementKind::Beam2D,
            timoshenko ? fem::core::BeamTheory::Timoshenko : fem::core::BeamTheory::EulerBernoulli);
        fem::core::test::fixAll(model, {1, 4}, fem::core::ElementKind::Beam2D);
        model.temperatureGradients.push_back({.elements = all, .alongY = gy});
        const auto result = fem::core::test::solve(model);
        for (std::size_t n = 0; n < 4; ++n) {
            CHECK(result.displacements[n].norm() <= 1e-14);
            CHECK(std::abs(result.rotations[n].z()) <= 1e-14);
        }
        for (std::size_t e = 0; e < 3; ++e) {
            const auto& beam = fem::core::test::beamOf(result, e);
            CHECK(near(beam.at(0.4 * beam.length()).forces.mz, kE * kIz * alpha * gy, 1e-10));
            CHECK(near(beam.start().vy, 0.0, 1e-10, kE * kIz * alpha * gy));
        }
    }
    SECTION("simply supported with a gradient: free curvature")
    {
        fem::core::Model model = simpleBeam(3, fem::core::BeamTheory::EulerBernoulli);
        model.temperatureGradients.push_back({.elements = all, .alongY = gy});
        const auto result = fem::core::test::solve(model);
        // rz' = -alpha gy: v = -alpha gy x (x - L) / 2, rz(0) = alpha gy L / 2.
        const double k = -alpha * gy;
        CHECK(near(result.rotations[0].z(), -k * kL / 2, 1e-10));
        const auto& middle = fem::core::test::beamOf(result, 1);
        const double x = kL / 2;
        CHECK(near(middle.at(middle.length() / 2).displacement.y(), k * x * (x - kL) / 2, 1e-10));
        CHECK(near(middle.at(0.3).forces.mz, 0.0, 1e-10, kE * kIz * alpha * gy));
    }
    SECTION("uniform temperature and initial strain held at both ends")
    {
        fem::core::Model model = fem::core::test::beamModel(row(3), fem::core::ElementKind::Beam3D);
        fem::core::test::fixAll(model, {1, 4}, fem::core::ElementKind::Beam3D);
        model.temperatureChanges.push_back({.elements = all, .deltaT = 25.0});
        model.initialStrains.push_back({.elements = fem::core::ElementIds{{2}}, .strain = 1e-4});
        const auto result = fem::core::test::solve(model);
        // In series: N = -EA (sum of the imposed elongations) / L.
        const double n = -kE * kArea * (alpha * 25.0 + 1e-4 / 3.0);
        for (std::size_t e = 0; e < 3; ++e) {
            CHECK(near(result.axialForce[e], n, 1e-10));
            CHECK(near(fem::core::test::beamOf(result, e).start().mz, 0.0, 1e-10, -n));
        }
    }
    SECTION("an L frame free at its tip only moves")
    {
        fem::core::test::TrussSpec spec;
        spec.nodes = {{0.0, 0.0, 0.0}, {0.0, 3.0, 0.0}, {kL, 3.0, 0.0}};
        spec.bars = {{0, 1}, {1, 2}};
        fem::core::Model model = fem::core::test::beamModel(spec, fem::core::ElementKind::Beam2D);
        fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam2D);
        model.temperatureChanges.push_back(
            {.elements = fem::core::ElementIds{{2}}, .deltaT = 40.0});
        const auto result = fem::core::test::solve(model);
        CHECK(near(result.displacements[2].x(), alpha * 40.0 * kL, 1e-10));
        CHECK(result.reactions[0].norm() <= 1e-6);
        CHECK(near(result.axialForce[1], 0.0, 1e-10, kE * kArea * alpha * 40.0));
    }
    SECTION("self-weight of an inclined cantilever")
    {
        const double angle = 0.6;
        fem::core::test::TrussSpec spec;
        spec.nodes = {{0.0, 0.0, 0.0}, {kL * std::cos(angle), kL * std::sin(angle), 0.0}};
        spec.bars = {{0, 1}};
        fem::core::Model model = fem::core::test::beamModel(spec, fem::core::ElementKind::Beam2D);
        fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam2D);
        model.gravity = Eigen::Vector3d(0.0, -9.81, 0.0);
        const auto result = fem::core::test::solve(model);
        const double weight = 7850.0 * kArea * 9.81 * kL;
        CHECK(near(result.reactions[0].y(), weight, 1e-10));
        CHECK(near(result.reactions[0].x(), 0.0, 1e-10, weight));
        CHECK(near(result.reactionMoments[0].z(), weight * kL * std::cos(angle) / 2, 1e-10));
        // Along the bar: the axial part of the weight, compression at the root.
        const auto& beam = fem::core::test::beamOf(result, 0);
        CHECK(near(beam.start().n, -weight * std::sin(angle), 1e-10));
    }
}
