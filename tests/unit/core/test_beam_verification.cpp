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
#include <cmath>

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
    Model model = fem::core::test::beamModel(cantileverSpec(1, length, Eigen::Vector3d::UnitX()),
                                             ElementKind::Beam2D,
                                             timoshenko ? BeamTheory::Timoshenko
                                                        : BeamTheory::EulerBernoulli);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0, p, 0)});
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
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Eigen::Vector3d(0, -p, 0)});
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
