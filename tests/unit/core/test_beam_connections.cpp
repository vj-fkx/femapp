// REQ-008-11, 12, 14, 19, NFR-008-01: releases, connection springs and rigid
// offsets in analyses.

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
#include <initializer_list>
#include <optional>
#include <utility>

using fem::core::BarEnd;
using fem::core::BeamTheory;
using fem::core::DofType;
using fem::core::ElementIds;
using fem::core::ElementKind;
using fem::core::EndConnection;
using fem::core::Model;
using fem::core::NodeIds;
using fem::core::test::kArea;
using fem::core::test::kE;
using fem::core::test::kG;
using fem::core::test::kIz;
using fem::core::test::kJ;
using fem::core::test::kShearArea;
using fem::core::test::near;
using fem::core::test::TrussSpec;

namespace {

constexpr double kTolerance = 1e-10;

TrussSpec rowSpec(std::initializer_list<double> xs)
{
    TrussSpec spec;
    for (const double x : xs) {
        spec.nodes.emplace_back(x, 0.0, 0.0);
    }
    for (int i = 0; i + 1 < static_cast<int>(xs.size()); ++i) {
        spec.bars.push_back({i, i + 1});
    }
    return spec;
}

EndConnection release(ElementIds elements, BarEnd end, int component, double stiffness = 0.0)
{
    EndConnection connection{.elements = std::move(elements), .end = end};
    connection.stiffness[static_cast<std::size_t>(component)] = stiffness;
    return connection;
}

constexpr int kMz = 5;
constexpr int kT = 3;

} // namespace

// REQ-008-11, 19: a Gerber beam, the hinge on either side of the node.
TEST_CASE("Gerber beam with an internal hinge", "[core][beam][connections]")
{
    const bool timoshenko = GENERATE(false, true);
    const bool onLoadedBar = GENERATE(false, true);
    const double a = 3.0;
    const double b = 4.0;
    const double w = 5e3;
    Model model = fem::core::test::beamModel(
        rowSpec({0.0, a, a + b}), ElementKind::Beam2D,
        timoshenko ? BeamTheory::Timoshenko : BeamTheory::EulerBernoulli);
    fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
    fem::core::test::restrain(model, {3}, {DofType::V});
    model.endConnections.push_back(onLoadedBar ? release(ElementIds{{2}}, BarEnd::Start, kMz)
                                               : release(ElementIds{{1}}, BarEnd::End, kMz));
    model.distributedLoads.push_back({.elements = ElementIds{{2}},
                                      .startValue = Eigen::Vector3d(0, -w, 0),
                                      .endValue = Eigen::Vector3d(0, -w, 0)});
    const auto result = fem::core::test::solve(model);
    // The hinge passes w b / 2 to the cantilever.
    const double f = w * b / 2;
    const double drop =
        f * a * a * a / (3 * kE * kIz) + (timoshenko ? f * a / (kG * kShearArea) : 0.0);
    CHECK(near(result.displacements[1].y(), -drop, kTolerance));
    CHECK(near(result.reactionMoments[0].z(), f * a, kTolerance));
    CHECK(near(result.reactions[2].y(), f, kTolerance));
    // No moment at the hinge, on both bars.
    CHECK(near(fem::core::test::beamOf(result, 0).end().mz, 0.0, kTolerance, f * a));
    CHECK(near(fem::core::test::beamOf(result, 1).start().mz, 0.0, kTolerance, f * a));
    CHECK(near(fem::core::test::beamOf(result, 1).at(b / 2).forces.mz, w * b * b / 8, kTolerance));
}

// REQ-008-11: bars hinged at both ends are a truss.
TEST_CASE("a truss of beams hinged at both ends", "[core][beam][connections]")
{
    TrussSpec spec;
    spec.nodes = {{0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {2.0, 3.0, 0.0}};
    spec.bars = {{0, 1}, {1, 2}, {2, 0}};
    Model beams = fem::core::test::beamModel(spec, ElementKind::Beam2D);
    beams.endConnections.push_back(release(ElementIds{{1, 2, 3}}, BarEnd::Both, kMz));
    fem::core::test::restrain(beams, {1}, {DofType::U, DofType::V});
    fem::core::test::restrain(beams, {2}, {DofType::V});
    // Every node is all hinges: its rotation is held.
    fem::core::test::restrain(beams, {1, 2, 3}, {DofType::RZ});
    beams.nodalForces.push_back({.nodes = NodeIds{{3}}, .force = Eigen::Vector3d(1e4, -2e4, 0)});
    Model truss = beams;
    truss.endConnections.clear();
    truss.restraints.resize(3);
    truss.assignments.front().kind = ElementKind::Truss2D;
    const auto a = fem::core::test::solve(beams);
    const auto b = fem::core::test::solve(truss);
    for (std::size_t n = 0; n < 3; ++n) {
        CHECK((a.displacements[n] - b.displacements[n]).norm() <=
              kTolerance * b.displacements[2].norm());
    }
    for (std::size_t e = 0; e < 3; ++e) {
        CHECK(near(a.axialForce[e], b.axialForce[e], kTolerance, 2e4));
        CHECK(near(fem::core::test::beamOf(a, e).at(0.5).forces.mz, 0.0, kTolerance, 2e4));
    }
}

// REQ-008-11: a torsion release sends the whole torque to the other side.
TEST_CASE("torsion release", "[core][beam][connections]")
{
    const double a = 2.0;
    const double b = 3.0;
    const double t = 4e3;
    Model model = fem::core::test::beamModel(rowSpec({0.0, a, a + b}), ElementKind::Beam3D);
    fem::core::test::fixAll(model, {1, 3}, ElementKind::Beam3D);
    model.nodalForces.push_back({.nodes = NodeIds{{2}}, .moment = Eigen::Vector3d(t, 0, 0)});
    // Without the release, the torque splits by stiffness.
    const auto both = fem::core::test::solve(model);
    CHECK(near(both.rotations[1].x(), t / (kG * kJ / a + kG * kJ / b), kTolerance));
    model.endConnections.push_back(release(ElementIds{{2}}, BarEnd::Start, kT));
    const auto released = fem::core::test::solve(model);
    CHECK(near(released.rotations[1].x(), t * a / (kG * kJ), kTolerance));
    CHECK(near(released.reactionMoments[2].x(), 0.0, kTolerance, t));
    CHECK(near(released.reactionMoments[0].x(), -t, kTolerance));
}

// REQ-008-12, 19: a semi-rigid root under a uniform load.
TEST_CASE("propped beam with a rotational spring at the root", "[core][beam][connections]")
{
    const double length = 5.0;
    const double w = 3e3;
    const double ei = kE * kIz;
    const auto rootMoment = [&](std::optional<double> stiffness) {
        Model model = fem::core::test::beamModel(rowSpec({0.0, length}), ElementKind::Beam2D);
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        fem::core::test::restrain(model, {2}, {DofType::V});
        if (stiffness) {
            model.endConnections.push_back(release(ElementIds{{1}}, BarEnd::Start, kMz, *stiffness));
        }
        model.distributedLoads.push_back({.elements = ElementIds{{1}},
                                          .startValue = Eigen::Vector3d(0, -w, 0),
                                          .endValue = Eigen::Vector3d(0, -w, 0)});
        const auto result = fem::core::test::solve(model);
        return fem::core::test::beamOf(result, 0).start().mz;
    };
    const double fixed = -w * length * length / 8;
    // M_A = -(w L^2 / 8) / (1 + 3 EI / (k L)).
    for (const double ratio : {0.5, 2.0, 10.0}) {
        const double k = ratio * ei / length;
        CHECK(near(rootMoment(k), fixed / (1.0 + 3.0 * ei / (k * length)), kTolerance));
    }
    CHECK(near(rootMoment(0.0), 0.0, kTolerance, -fixed));
    CHECK(near(rootMoment(1e8 * ei / length), fixed, 1e-6));
    CHECK(near(rootMoment(std::nullopt), fixed, kTolerance));
}

// REQ-008-14, 19: an offset is a rigid bar.
TEST_CASE("rigid offsets against explicit rigid bars", "[core][beam][connections]")
{
    const bool timoshenko = GENERATE(false, true);
    const BeamTheory theory = timoshenko ? BeamTheory::Timoshenko : BeamTheory::EulerBernoulli;
    const double length = 5.0;
    const Eigen::Vector3d start(0.3, 0.4, 0.0);
    const Eigen::Vector3d end(-0.2, 0.1, 0.0);
    const Eigen::Vector3d tipForce(1e3, -5e3, 0.0);
    const Eigen::Vector3d load(0.0, -2e3, 0.0);

    Model offset = fem::core::test::beamModel(rowSpec({0.0, length}), ElementKind::Beam2D, theory);
    offset.rigidOffsets.push_back({.elements = ElementIds{{1}}, .start = start, .end = end});
    fem::core::test::fixAll(offset, {1}, ElementKind::Beam2D);
    offset.nodalForces.push_back({.nodes = NodeIds{{2}}, .force = tipForce});
    offset.distributedLoads.push_back(
        {.elements = ElementIds{{1}}, .startValue = load, .endValue = load});

    // The same with the links as bars `ratio` times stiffer than the beam.
    const auto explicitModel = [&](double ratio) {
        TrussSpec spec;
        spec.nodes = {
            {0.0, 0.0, 0.0}, start, Eigen::Vector3d(length, 0, 0) + end, {length, 0.0, 0.0}};
        spec.bars = {{0, 1}, {1, 2}, {2, 3}};
        Model model = fem::core::test::beamModel(spec, ElementKind::Beam2D, theory);
        fem::core::Section rigid = fem::core::test::genericSection();
        rigid.name = "Rigid";
        rigid.area = kArea * ratio;
        rigid.inertiaZ = kIz * ratio;
        rigid.shearAreaY = rigid.shearAreaZ = kShearArea * ratio;
        model.sections.push_back(rigid);
        model.assignments.front().elements = ElementIds{{2}};
        model.assignments.push_back({.elements = ElementIds{{1, 3}},
                                     .kind = ElementKind::Beam2D,
                                     .material = "Steel",
                                     .section = "Rigid",
                                     .theory = theory});
        fem::core::test::fixAll(model, {1}, ElementKind::Beam2D);
        model.nodalForces.push_back({.nodes = NodeIds{{4}}, .force = tipForce});
        model.distributedLoads.push_back(
            {.elements = ElementIds{{2}}, .startValue = load, .endValue = load});
        return fem::core::test::solve(model);
    };

    const auto a = fem::core::test::solve(offset);
    // The links bend and stretch by 1/ratio: Richardson extrapolation of
    // ratios 1e4 and 1e5 removes that term (larger ratios lose digits to the
    // conditioning).
    const auto b4 = explicitModel(1e4);
    const auto b5 = explicitModel(1e5);
    const auto rigid = [](double coarse, double fine) { return fine + (fine - coarse) / 9.0; };
    const Eigen::Vector3d tip = b5.displacements[3] + (b5.displacements[3] - b4.displacements[3]) / 9.0;
    CHECK((a.displacements[1] - tip).norm() <= 1e-6 * tip.norm());
    CHECK(near(a.rotations[1].z(), rigid(b4.rotations[3].z(), b5.rotations[3].z()), 1e-6));
    // Equilibrium: the reactions hold the loads exactly.
    const double flexibleLength = (Eigen::Vector3d(length, 0, 0) + end - start).norm();
    CHECK((a.reactions[0] + tipForce + load * flexibleLength).norm() <= 1e-10 * tipForce.norm());
    CHECK(near(a.reactionMoments[0].z(),
               rigid(b4.reactionMoments[0].z(), b5.reactionMoments[0].z()), 1e-6));
    const auto& flexible = fem::core::test::beamOf(a, 0);
    CHECK(near(flexible.length(), flexibleLength, 1e-14));
    CHECK(near(flexible.start().mz,
               rigid(fem::core::test::beamOf(b4, 1).start().mz,
                     fem::core::test::beamOf(b5, 1).start().mz),
               1e-6));
    CHECK(near(flexible.at(1.0).forces.vy,
               rigid(fem::core::test::beamOf(b4, 1).at(1.0).forces.vy,
                     fem::core::test::beamOf(b5, 1).at(1.0).forces.vy),
               1e-6));
}
