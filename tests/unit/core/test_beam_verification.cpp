// REQ-008-07, 08, 20, 21, NFR-008-01: beam analyses against closed-form
// solutions.

// GCC 13 at -O2 warns that the std::vector of the inactive NodeIds or
// ElementIds alternative "may be used uninitialized" when a temporary holding
// a GroupRef is moved into the model: a false positive of its variant
// analysis.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include "beam_models.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Core>
#include <Eigen/LU>
#include <array>
#include <cmath>
#include <complex>
#include <numbers>

using fem::core::BeamTheory;
using fem::core::DofType;
using fem::core::ElementKind;
using fem::core::Model;
using fem::core::test::kArea;
using fem::core::test::kE;
using fem::core::test::kG;
using fem::core::test::kIy;
using fem::core::test::kIz;
using fem::core::test::kJ;
using fem::core::test::kShearArea;
using fem::core::test::near;
using fem::core::test::TrussSpec;

namespace {

constexpr double kTolerance = 1e-10;

TrussSpec cantileverSpec(int pieces, double length, const Eigen::Vector3d& direction)
{
    TrussSpec spec;
    for (int i = 0; i <= pieces; ++i) {
        spec.nodes.emplace_back(direction * (length * i / pieces));
    }
    for (int i = 0; i < pieces; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    return spec;
}

} // namespace

// REQ-008-21: a 2D cantilever with a tip force, both theories.
TEST_CASE("2D cantilever with a tip force", "[core][beam][verification]")
{
    const bool timoshenko = GENERATE(false, true);
    const double length = 3.0;
    const double p = 1.0e4;
    Model model = fem::core::test::beamModel(
        cantileverSpec(1, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D,
        timoshenko ? BeamTheory::Timoshenko : BeamTheory::EulerBernoulli);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0, p, 0)});
    const auto result = fem::core::test::solve(model);

    const double shear = timoshenko ? p * length / (kG * kShearArea) : 0.0;
    const double tip = p * length * length * length / (3 * kE * kIz) + shear;
    CHECK(near(result.displacements[1].y(), tip, kTolerance));
    CHECK(near(result.rotations[1].z(), p * length * length / (2 * kE * kIz), kTolerance));
    CHECK(near(result.reactions[0].y(), -p, kTolerance));
    CHECK(near(result.reactionMoments[0].z(), -p * length, kTolerance));
    const auto& beam = fem::core::test::beamOf(result, 0);
    CHECK(near(beam.start().vy, p, kTolerance));
    CHECK(near(beam.start().mz, p * length, kTolerance));
    CHECK(near(beam.end().mz, 0.0, kTolerance, p * length));
    CHECK(near(result.axialForce[0], 0.0, kTolerance, p));
}

// REQ-008-21: a 3D cantilever with forces and a torque at the tip.
TEST_CASE("3D cantilever with forces and a torque", "[core][beam][verification]")
{
    const double length = 2.0;
    const double py = 3.0e3;
    const double pz = -5.0e3;
    const double t = 1.0e3;
    const double n = 2.0e5;
    Model model = fem::core::test::beamModel(cantileverSpec(2, length, Eigen::Vector3d::UnitX()),
                                             ElementKind::Beam3D);
    // Local y = global Y, z = Z.
    model.assignments.front().reference = Eigen::Vector3d::UnitY();
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{3}},
                                 .force = Eigen::Vector3d(n, py, pz),
                                 .moment = Eigen::Vector3d(t, 0, 0)});
    const auto result = fem::core::test::solve(model);
    const Eigen::Vector3d u = result.displacements[2];
    const Eigen::Vector3d r = result.rotations[2];
    const double l3 = length * length * length;
    CHECK(near(u.x(), n * length / (kE * kArea), kTolerance));
    CHECK(near(u.y(), py * l3 / (3 * kE * kIz), kTolerance));
    CHECK(near(u.z(), pz * l3 / (3 * kE * kIy), kTolerance));
    CHECK(near(r.x(), t * length / (kG * kJ), kTolerance));
    CHECK(near(r.y(), -pz * length * length / (2 * kE * kIy), kTolerance));
    CHECK(near(r.z(), py * length * length / (2 * kE * kIz), kTolerance));
    CHECK(near(result.reactionMoments[0].x(), -t, kTolerance));
    CHECK(near(result.reactionMoments[0].y(), pz * length, kTolerance));
    // Internal forces at the root: My = -Pz L stretches z > 0 under Pz < 0.
    const auto& root = fem::core::test::beamOf(result, 0);
    CHECK(near(root.start().n, n, kTolerance));
    CHECK(near(root.start().vz, pz, kTolerance));
    CHECK(near(root.start().t, t, kTolerance));
    CHECK(near(root.start().my, -pz * length, kTolerance));
}

// REQ-008-20: a cantilever held by a truss tie at its tip.
TEST_CASE("a beam held by a truss tie", "[core][beam][verification]")
{
    const double length = 4.0;
    const double height = 2.0;
    const double p = 2.0e4;
    TrussSpec spec = cantileverSpec(1, length, Eigen::Vector3d::UnitX());
    spec.nodes.emplace_back(length, height, 0.0);
    spec.bars.push_back({1, 2});
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    model.assignments.front().elements = fem::core::ElementIds{{1}};
    const double tieArea = 1e-4;
    model.sections.push_back({.name = "Rod", .area = tieArea});
    model.assignments.push_back({.elements = fem::core::ElementIds{{2}},
                                 .kind = ElementKind::Truss2D,
                                 .material = "Steel",
                                 .section = "Rod"});
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    fem::core::test::restrain(model, {3}, {DofType::U, DofType::V});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0, -p, 0)});
    const auto result = fem::core::test::solve(model);

    const double beam = 3 * kE * kIz / (length * length * length);
    const double tie = kE * tieArea / height;
    const double drop = p / (beam + tie);
    CHECK(near(result.displacements[1].y(), -drop, kTolerance));
    CHECK(near(result.axialForce[1], tie * drop, kTolerance));
    CHECK(near(result.reactions[2].y(), tie * drop, kTolerance));
    CHECK_FALSE(result.beams[1].has_value());
    CHECK(result.rotations[2] == Eigen::Vector3d::Zero()); // a truss node: no rotation
}

namespace {

// A straight bar of unequal pieces along x.
TrussSpec unequalSpec(double length)
{
    TrussSpec spec;
    for (const double f : {0.0, 0.13, 0.4, 0.62, 1.0}) {
        spec.nodes.emplace_back(f * length, 0.0, 0.0);
    }
    spec.bars = {{0, 1}, {1, 2}, {2, 3}, {3, 4}};
    return spec;
}

fem::core::ElementIds allBars(int count)
{
    fem::core::ElementIds ids;
    for (int i = 1; i <= count; ++i) {
        ids.ids.push_back(i);
    }
    return ids;
}

} // namespace

// REQ-008-08, NFR-008-01: constant moment, normal force and torsion.
TEST_CASE("beam patch test", "[core][beam][verification]")
{
    const bool timoshenko = GENERATE(false, true);
    const double length = 5.0;
    const double n = 3e5;
    const double t = 2e3;
    const double my = -4e3;
    const double mz = 7e3;
    Model model = fem::core::test::beamModel(unequalSpec(length), ElementKind::Beam3D,
                                             timoshenko ? BeamTheory::Timoshenko
                                                        : BeamTheory::EulerBernoulli);
    model.assignments.front().reference = Eigen::Vector3d::UnitY(); // local = global
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{5}},
                                 .force = Eigen::Vector3d(n, 0, 0),
                                 .moment = Eigen::Vector3d(t, my, mz)});
    const auto result = fem::core::test::solve(model);
    for (std::size_t i = 0; i < 5; ++i) {
        const double x = model.mesh->nodeCoordinates(static_cast<fem::core::NodeIndex>(i)).x();
        const Eigen::Vector3d u = result.displacements[i];
        const Eigen::Vector3d r = result.rotations[i];
        CHECK(near(u.x(), n * x / (kE * kArea), kTolerance, n * length / (kE * kArea)));
        CHECK(near(u.y(), mz * x * x / (2 * kE * kIz), kTolerance,
                   mz * length * length / (kE * kIz)));
        CHECK(near(u.z(), -my * x * x / (2 * kE * kIy), kTolerance,
                   -my * length * length / (kE * kIy)));
        CHECK(near(r.x(), t * x / (kG * kJ), kTolerance, t * length / (kG * kJ)));
        CHECK(near(r.y(), my * x / (kE * kIy), kTolerance, -my * length / (kE * kIy)));
        CHECK(near(r.z(), mz * x / (kE * kIz), kTolerance, mz * length / (kE * kIz)));
    }
    for (std::size_t e = 0; e < 4; ++e) {
        const auto& beam = fem::core::test::beamOf(result, e);
        for (const auto& f : {beam.start(), beam.end(), beam.at(0.37 * beam.length()).forces}) {
            CHECK(near(f.n, n, kTolerance));
            CHECK(near(f.t, t, kTolerance));
            CHECK(near(f.my, my, kTolerance));
            CHECK(near(f.mz, mz, kTolerance));
            CHECK(near(f.vy, 0.0, kTolerance, mz));
            CHECK(near(f.vz, 0.0, kTolerance, mz));
        }
    }
}

// REQ-008-08, 16, 21, 22: classic beams, both theories.
TEST_CASE("analytical beams", "[core][beam][verification]")
{
    const bool timoshenko = GENERATE(false, true);
    const BeamTheory theory = timoshenko ? BeamTheory::Timoshenko : BeamTheory::EulerBernoulli;
    const double ei = kE * kIz;
    const double gas = kG * kShearArea;
    const double shear = timoshenko ? 1.0 / gas : 0.0;
    const double length = 6.0;
    const double w = 4e3;
    const double p = 9e3;

    SECTION("cantilever with a tip moment")
    {
        const double m = 5e3;
        Model model = fem::core::test::beamModel(
            cantileverSpec(3, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{4}}, .moment = Eigen::Vector3d(0, 0, m)});
        const auto result = fem::core::test::solve(model);
        CHECK(near(result.displacements[3].y(), m * length * length / (2 * ei), kTolerance));
        CHECK(near(result.rotations[3].z(), m * length / ei, kTolerance));
        CHECK(near(result.reactionMoments[0].z(), -m, kTolerance));
    }
    SECTION("fixed-fixed with a uniform load")
    {
        Model model = fem::core::test::beamModel(
            cantileverSpec(2, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::fixAll(model, {1, 3}, ElementKind::Beam2D);
        model.distributedLoads.push_back({.elements = allBars(2),
                                          .startValue = Eigen::Vector3d(0, -w, 0),
                                          .endValue = Eigen::Vector3d(0, -w, 0)});
        const auto result = fem::core::test::solve(model);
        const double mid = -w * std::pow(length, 4) / (384 * ei) - w * length * length / 8 * shear;
        CHECK(near(result.displacements[1].y(), mid, kTolerance));
        CHECK(near(result.reactionMoments[0].z(), w * length * length / 12, kTolerance));
        CHECK(near(result.reactions[2].y(), w * length / 2, kTolerance));
        const auto& left = fem::core::test::beamOf(result, 0);
        CHECK(near(left.start().mz, -w * length * length / 12, kTolerance));
        CHECK(near(left.end().mz, w * length * length / 24, kTolerance));
    }
    SECTION("fixed-fixed with a force at mid-span")
    {
        Model model = fem::core::test::beamModel(
            cantileverSpec(1, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::fixAll(model, {1, 2}, ElementKind::Beam2D);
        model.concentratedLoads.push_back(
            {.elements = allBars(1), .force = Eigen::Vector3d(0, -p, 0), .at = {.value = 0.5}});
        const auto result = fem::core::test::solve(model);
        const auto& beam = fem::core::test::beamOf(result, 0);
        const double mid = -p * std::pow(length, 3) / (192 * ei) - p * length / 4 * shear;
        CHECK(near(beam.at(length / 2).displacement.y(), mid, kTolerance));
        CHECK(near(beam.start().mz, -p * length / 8, kTolerance));
        CHECK(near(beam.at(length / 2).forces.mz, p * length / 8, kTolerance));
    }
    SECTION("simply supported with a triangular load")
    {
        Model model = fem::core::test::beamModel(
            cantileverSpec(2, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {3}, {DofType::V});
        // 0 at the left support to w at the right one, over two bars.
        model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                          .startValue = Eigen::Vector3d::Zero(),
                                          .endValue = Eigen::Vector3d(0, -w / 2, 0)});
        model.distributedLoads.push_back({.elements = fem::core::ElementIds{{2}},
                                          .startValue = Eigen::Vector3d(0, -w / 2, 0),
                                          .endValue = Eigen::Vector3d(0, -w, 0)});
        const auto result = fem::core::test::solve(model);
        CHECK(near(result.reactions[0].y(), w * length / 6, kTolerance));
        CHECK(near(result.reactions[2].y(), w * length / 3, kTolerance));
        // M = w L x / 6 - w x^3 / (6 L);
        // v = -w x (7 L^4 - 10 L^2 x^2 + 3 x^4) / (360 EI L) - M / GAs.
        for (std::size_t e = 0; e < 2; ++e) {
            const auto& beam = fem::core::test::beamOf(result, e);
            for (int k = 0; k <= 6; ++k) {
                const double s = beam.length() * k / 6.0;
                const double x = 3.0 * static_cast<double>(e) + s;
                const double m = w * length * x / 6 - w * x * x * x / (6 * length);
                const double v = -w * x *
                                     (7 * std::pow(length, 4) - 10 * length * length * x * x +
                                      3 * std::pow(x, 4)) /
                                     (360 * ei * length) -
                                 m * shear;
                const auto point = beam.at(s);
                CHECK(near(point.forces.mz, m, kTolerance, w * length * length));
                CHECK(near(point.displacement.y(), v, kTolerance, w * std::pow(length, 4) / ei));
            }
        }
    }
    SECTION("partial load: reactions and moments by statics")
    {
        Model model = fem::core::test::beamModel(
            cantileverSpec(1, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::restrain(model, {1}, {DofType::U, DofType::V});
        fem::core::test::restrain(model, {2}, {DofType::V});
        const double a = 1.5;
        const double b = 4.0;
        model.distributedLoads.push_back({.elements = allBars(1),
                                          .startValue = Eigen::Vector3d(0, -w, 0),
                                          .endValue = Eigen::Vector3d(0, -2 * w, 0),
                                          .from = {.value = a, .fraction = false},
                                          .to = {.value = b, .fraction = false}});
        const auto result = fem::core::test::solve(model);
        // Trapezoid from w to 2w between a and b: total and centroid.
        const double total = 1.5 * w * (b - a);
        const double centre = a + (b - a) * (1.0 + 2.0 * 2.0) / (3.0 * (1.0 + 2.0));
        const double left = total * (length - centre) / length;
        CHECK(near(result.reactions[0].y(), left, kTolerance));
        const auto& beam = fem::core::test::beamOf(result, 0);
        // Past the load: M = R_right (L - x); before it: M = R_left x.
        CHECK(near(beam.at(5.0).forces.mz, (total - left) * (length - 5.0), kTolerance));
        CHECK(near(beam.at(1.0).forces.mz, left, kTolerance));
    }
    SECTION("settlement and prescribed rotation")
    {
        const double delta = 0.01;
        const double theta = 0.002;
        const double phi = timoshenko ? 12 * ei / (gas * length * length) : 0.0;
        Model model = fem::core::test::beamModel(
            cantileverSpec(1, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::restrain(model, {1, 2}, {DofType::U});
        fem::core::test::restrain(model, {1}, {DofType::V, DofType::RZ});
        fem::core::test::restrain(model, {2}, {DofType::V}, -delta);
        fem::core::test::restrain(model, {2}, {DofType::RZ}, theta);
        const auto result = fem::core::test::solve(model);
        // From the stiffness of the fixed-fixed bar, with c = EI / (1 + phi):
        // V2 = 12 c v2 / L^3 - 6 c t2 / L^2, M1 = -6 c v2 / L^2 + (2 - phi) c t2 / L.
        const double c = ei / (1 + phi);
        CHECK(near(result.reactions[1].y(),
                   -12 * c * delta / std::pow(length, 3) - 6 * c * theta / (length * length),
                   kTolerance));
        CHECK(near(result.reactionMoments[0].z(),
                   6 * c * delta / (length * length) + (2 - phi) * c * theta / length, kTolerance));
    }
}

// REQ-008-08: a continuous beam of two equal spans (Euler-Bernoulli).
TEST_CASE("continuous beam of two spans", "[core][beam][verification]")
{
    const double span = 5.0;
    const double w = 6e3;
    Model model = fem::core::test::beamModel(cantileverSpec(2, 2 * span, Eigen::Vector3d::UnitX()),
                                             ElementKind::Beam2D);
    fem::core::test::restrain(model, {1}, {DofType::U});
    fem::core::test::restrain(model, {1, 2, 3}, {DofType::V});
    model.distributedLoads.push_back({.elements = allBars(2),
                                      .startValue = Eigen::Vector3d(0, -w, 0),
                                      .endValue = Eigen::Vector3d(0, -w, 0)});
    const auto result = fem::core::test::solve(model);
    CHECK(near(result.reactions[0].y(), 3 * w * span / 8, kTolerance));
    CHECK(near(result.reactions[1].y(), 5 * w * span / 4, kTolerance));
    CHECK(near(fem::core::test::beamOf(result, 0).end().mz, -w * span * span / 8, kTolerance));
    CHECK(near(result.rotations[1].z(), 0.0, kTolerance, w * span * span * span / (kE * kIz)));
}

// REQ-008-08, 20, 21: frames and grids against virtual work.
TEST_CASE("plane L frame with lateral and vertical loads", "[core][beam][verification]")
{
    const bool timoshenko = GENERATE(false, true);
    const double h = 4.0;
    const double l = 3.0;
    const Eigen::Vector2d force(6e3, -9e3); // at the tip
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {0.0, h / 2, 0.0}, {0.0, h, 0.0}, {l, h, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {2, 3}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D,
                                             timoshenko ? BeamTheory::Timoshenko
                                                        : BeamTheory::EulerBernoulli);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{4}}, .force = Eigen::Vector3d(force.x(), force.y(), 0)});
    const auto result = fem::core::test::solve(model);
    // Unit loads along x and y at the tip: bending, axial and shear work.
    const double ei = kE * kIz;
    const double ea = kE * kArea;
    const double gas = timoshenko ? kG * kShearArea : 0.0;
    const auto work = [&](const Eigen::Vector2d& d) {
        double delta = force.y() * d.y() * l * l * l / (3 * ei);
        delta += (l * l * force.y() * d.y() * h -
                  l * (force.y() * d.x() + force.x() * d.y()) * h * h / 2 +
                  force.x() * d.x() * h * h * h / 3) /
                 ei;
        delta += force.x() * d.x() * l / ea + force.y() * d.y() * h / ea;
        if (gas > 0.0) {
            delta += force.y() * d.y() * l / gas + force.x() * d.x() * h / gas;
        }
        return delta;
    };
    CHECK(near(result.displacements[3].x(), work({1.0, 0.0}), kTolerance));
    CHECK(near(result.displacements[3].y(), work({0.0, 1.0}), kTolerance));
    CHECK(near(result.reactionMoments[0].z(), -(l * force.y() - h * force.x()), kTolerance));
}

TEST_CASE("space L frame with bending and torsion", "[core][beam][verification]")
{
    const bool timoshenko = GENERATE(false, true);
    const double a = 3.0; // along x, fixed at the origin
    const double b = 2.0; // along y
    const double p = -5e3;
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {a, 0.0, 0.0}, {a, b, 0.0}};
    spec.bars = {{0, 1}, {1, 2}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam3D,
                                             timoshenko ? BeamTheory::Timoshenko
                                                        : BeamTheory::EulerBernoulli);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{3}}, .force = Eigen::Vector3d(0, 0, p)});
    const auto result = fem::core::test::solve(model);
    // Horizontal bars: the default local y is vertical, so Iz bends them.
    const double ei = kE * kIz;
    double expected = p * (a * a * a + b * b * b) / (3 * ei) + p * b * b * a / (kG * kJ);
    if (timoshenko) {
        expected += p * (a + b) / (kG * kShearArea);
    }
    CHECK(near(result.displacements[2].z(), expected, kTolerance));
    // The arm along y twists the one along x by p b.
    CHECK(
        near(std::abs(fem::core::test::beamOf(result, 0).start().t), std::abs(p * b), kTolerance));
    CHECK(near(result.reactionMoments[0].x(), -p * b, kTolerance));
}

TEST_CASE("grid of two crossing beams", "[core][beam][verification]")
{
    const double a = 4.0; // half span along x
    const double b = 3.0; // half span along y
    const double p = -2e4;
    TrussSpec spec;
    spec.nodes = {{-a, 0, 0}, {0, 0, 0}, {a, 0, 0}, {0, -b, 0}, {0, b, 0}};
    spec.bars = {{0, 1}, {1, 2}, {3, 1}, {1, 4}};
    Model model = fem::core::test::beamModel(spec, ElementKind::Beam3D);
    fem::core::test::restrain(model, {1, 3, 4, 5}, {DofType::W});
    fem::core::test::restrain(model, {1, 2, 3, 4, 5}, {DofType::U, DofType::V, DofType::RZ});
    model.nodalForces.push_back(
        {.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0, 0, p)});
    const auto result = fem::core::test::solve(model);
    const double kx = 48 * kE * kIz / std::pow(2 * a, 3);
    const double ky = 48 * kE * kIz / std::pow(2 * b, 3);
    CHECK(near(result.displacements[1].z(), p / (kx + ky), kTolerance));
    CHECK(near(result.reactions[0].z(), -p * kx / (kx + ky) / 2, kTolerance));
}

// REQ-008-07: Timoshenko shear deflection and the slender limit.
TEST_CASE("Timoshenko against Euler-Bernoulli", "[core][beam][verification]")
{
    const double p = 1e3;
    const auto tip = [p](double length, BeamTheory theory) {
        Model model = fem::core::test::beamModel(
            cantileverSpec(4, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D, theory);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{5}}, .force = Eigen::Vector3d(0, p, 0)});
        return fem::core::test::solve(model).displacements[4].y();
    };
    // Short: the depth sqrt(12 Iz / A) is 0.31, the length 0.6.
    const double s = 0.6;
    CHECK(near(tip(s, BeamTheory::Timoshenko),
               p * s * s * s / (3 * kE * kIz) + p * s / (kG * kShearArea), kTolerance));
    CHECK(tip(s, BeamTheory::Timoshenko) > 1.1 * tip(s, BeamTheory::EulerBernoulli));
    // Slender, L / h = 1000: no locking.
    const double l = 310.0;
    CHECK(near(tip(l, BeamTheory::Timoshenko), tip(l, BeamTheory::EulerBernoulli), 1e-6));
}

// REQ-008-05, 09: asymmetric sections bend about their principal axes and
// twist about their shear centre.
TEST_CASE("cantilevers of asymmetric sections", "[core][beam][verification]")
{
    const double length = 2.5;
    const bool angle = GENERATE(true, false);
    fem::core::Section section{.name = "S"};
    if (angle) {
        section.shape = fem::core::Angle{.b = 0.1, .h = 0.15, .t = 0.012};
    } else {
        section.shape = fem::core::Channel{.h = 0.2, .bf = 0.075, .tf = 0.0115, .tw = 0.0085};
    }
    const auto properties =
        fem::core::sectionProperties(section).value_or(fem::core::SectionProperties{});
    const Eigen::Vector3d force(0.0, 4e3, -3e3); // at the tip, on the centroid
    const auto solveWith = [&](const Eigen::Vector3d& moment) {
        Model model = fem::core::test::beamModel(
            cantileverSpec(3, length, Eigen::Vector3d::UnitX()), ElementKind::Beam3D);
        model.sections.front() = section;
        model.assignments.front().reference = Eigen::Vector3d::UnitY(); // local = global
        fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{4}}, .force = force, .moment = moment});
        return fem::core::test::solve(model);
    };
    const auto result = solveWith(Eigen::Vector3d::Zero());

    // In the principal axes: y' = c y + s z.
    const double c = std::cos(properties.principalAngle);
    const double s = std::sin(properties.principalAngle);
    const double py = c * force.y() + s * force.z();
    const double pz = -s * force.y() + c * force.z();
    const double ey = properties.shearCentre.x();
    const double ez = properties.shearCentre.y();
    const double torque = ez * py - ey * pz; // (C - S) x P
    const double twist = torque * length / (kG * properties.torsionConstant);
    const double l3 = length * length * length;
    const double vs = py * l3 / (3 * kE * properties.inertiaZ);
    const double ws = pz * l3 / (3 * kE * properties.inertiaY);
    const double vc = vs + ez * twist; // the centroid turns about the shear centre
    const double wc = ws - ey * twist;
    const Eigen::Vector3d tip = result.displacements[3];
    const double scale = std::abs(vs) + std::abs(ws);
    CHECK(near(tip.y(), c * vc - s * wc, kTolerance, scale));
    CHECK(near(tip.z(), s * vc + c * wc, kTolerance, scale));
    CHECK(near(result.rotations[3].x(), twist, kTolerance));
    CHECK(std::abs(twist) > 1e-4); // the eccentricity matters here
    if (angle) {
        CHECK(std::abs(properties.principalAngle) > 0.1);
    }

    // The same force through the shear centre does not twist: add the torque
    // that moves it there.
    const auto centred = solveWith(Eigen::Vector3d(-torque, 0.0, 0.0));
    CHECK(near(centred.rotations[3].x(), 0.0, kTolerance, std::abs(twist)));
    CHECK(near(centred.displacements[3].y(), c * vs - s * ws, kTolerance, scale));
}

namespace {

// Errors to a reference at three refinements; the rate of the last pair.
double rate(const std::array<double, 3>& errors)
{
    return std::log2(errors[1] / errors[2]);
}

} // namespace

// REQ-008-08: straight bars converge to a curved beam (quarter ring).
TEST_CASE("convergence on a quarter ring", "[core][beam][verification]")
{
    const double radius = 2.0;
    const double p = 1e4;
    // Castigliano on the thin ring: (pi / 4) (P R^3 / EI + P R / EA).
    const double exact = std::numbers::pi / 4 *
                         (p * radius * radius * radius / (kE * kIz) + p * radius / (kE * kArea));
    std::array<double, 3> errors{};
    const std::array<int, 3> pieces = {8, 16, 32};
    for (std::size_t k = 0; k < pieces.size(); ++k) {
        const int n = pieces[k];
        TrussSpec spec;
        for (int i = 0; i <= n; ++i) {
            const double phi = std::numbers::pi / 2 * i / n;
            spec.nodes.emplace_back(radius * std::cos(phi), radius * std::sin(phi), 0.0);
        }
        for (int i = 0; i < n; ++i) {
            spec.bars.push_back({i, i + 1});
        }
        Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(n + 1)}},
             .force = Eigen::Vector3d(0, p, 0)});
        const auto result = fem::core::test::solve(model);
        errors.at(k) = std::abs(result.displacements[static_cast<std::size_t>(n)].y() - exact);
    }
    INFO("errors " << errors[0] << ", " << errors[1] << ", " << errors[2] << "; rate "
                   << rate(errors));
    CHECK(errors[2] < 1e-2 * exact);
    CHECK(std::abs(rate(errors) - 2.0) < 0.2);
}

// REQ-008-15: a free beam on an elastic foundation with a central force
// (Hetenyi), against the exact solution of EI w'''' + k w = 0.
TEST_CASE("convergence on an elastic foundation", "[core][beam][verification]")
{
    const double length = 8.0;
    const double beta = 0.5; // beta L = 4
    const double ei = kE * kIz;
    const double k = 4 * ei * std::pow(beta, 4);
    const double p = 1e5;
    // Half beam from the centre: w'(0) = 0, EI w'''(0) = P / 2, w''(L/2) = w'''(L/2) = 0,
    // w downwards; w = sum of Re, Im of exp(lambda x), lambda = beta (+-1 + i).
    using Complex = std::complex<double>;
    const std::array<Complex, 2> lambdas = {Complex(beta, beta), Complex(-beta, beta)};
    const auto basis = [&](int j, int derivative, double x) {
        const Complex l = lambdas.at(static_cast<std::size_t>(j / 2));
        const Complex value = std::pow(l, derivative) * std::exp(l * x);
        return j % 2 == 0 ? value.real() : value.imag();
    };
    Eigen::Matrix4d a;
    Eigen::Vector4d rhs(0.0, p / 2 / ei, 0.0, 0.0);
    for (int j = 0; j < 4; ++j) {
        a(0, j) = basis(j, 1, 0.0);
        a(1, j) = basis(j, 3, 0.0);
        a(2, j) = basis(j, 2, length / 2);
        a(3, j) = basis(j, 3, length / 2);
    }
    const Eigen::Vector4d coefficients = a.fullPivLu().solve(rhs);
    double exact = 0.0;
    for (int j = 0; j < 4; ++j) {
        exact += coefficients(j) * basis(j, 0, 0.0);
    }
    REQUIRE(exact > 0.0);
    // Close to the infinite beam, P beta / (2 k), for beta L = 4.
    CHECK(std::abs(exact / (p * beta / (2 * k)) - 1.0) < 0.2);

    std::array<double, 3> errors{};
    const std::array<int, 3> pieces = {4, 8, 16};
    for (std::size_t m = 0; m < pieces.size(); ++m) {
        const int n = pieces[m];
        Model model = fem::core::test::beamModel(
            cantileverSpec(n, length, Eigen::Vector3d::UnitX()), ElementKind::Beam2D);
        fem::core::ElementIds all;
        for (int i = 1; i <= n; ++i) {
            all.ids.push_back(i);
        }
        model.foundations.push_back({.elements = all, .translational = Eigen::Vector3d(0, k, 0)});
        const auto centre = static_cast<fem::core::ExternalId>(n / 2 + 1);
        fem::core::test::restrain(model, {centre}, {DofType::U});
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{centre}}, .force = Eigen::Vector3d(0, -p, 0)});
        const auto result = fem::core::test::solve(model);
        errors.at(m) = std::abs(-result.displacements[static_cast<std::size_t>(n / 2)].y() - exact);
    }
    INFO("exact " << exact << "; errors " << errors[0] << ", " << errors[1] << ", " << errors[2]
                  << "; rate " << rate(errors));
    CHECK(errors[2] < 1e-3 * exact);
    CHECK(std::abs(rate(errors) - 4.0) < 0.3);
}
