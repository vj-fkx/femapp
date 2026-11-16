// REQ-008b-07 to 11: beams with warping (design 008b §5.3, 5.4).

#include "beam_models.hpp"

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <vector>

using fem::core::BeamData;
using fem::core::ElementKind;
using fem::core::ExactBeamElement;
using fem::core::SpanLoads;
using fem::core::WarpingCondition;

namespace {

BeamData sampleBeam()
{
    BeamData beam;
    beam.axial = 4.0e6;
    beam.torsional = 3.0e3;
    beam.bendingY = 2.0e4;
    beam.bendingZ = 5.0e4;
    beam.shearY = 1.0e6;
    beam.shearZ = 8.0e5;
    beam.shearCentre = Eigen::Vector2d(0.02, -0.01);
    return beam;
}

// Turned axes for a bar from a to b.
Eigen::Matrix3d axesOf(const Eigen::Vector3d& a, const Eigen::Vector3d& b)
{
    const Eigen::Vector3d x = (b - a).normalized();
    const Eigen::Vector3d y = Eigen::Vector3d::UnitZ().cross(x).normalized();
    Eigen::Matrix3d axes;
    axes << x, y, x.cross(y);
    return axes * Eigen::AngleAxisd(0.3, Eigen::Vector3d::UnitX()).toRotationMatrix();
}

struct Computed
{
    Eigen::MatrixXd matrix;
    Eigen::VectorXd vector;
};

Computed exact(const BeamData& beam, const SpanLoads& loads, const Eigen::Vector3d& a,
               const Eigen::Vector3d& b, std::array<bool, 2> continuous, int size)
{
    const std::vector<BeamData> beams = {beam};
    const std::vector<SpanLoads> spans = {loads};
    const ExactBeamElement element(ElementKind::Beam3D, continuous, beams, spans);
    Computed out{.matrix = Eigen::MatrixXd::Zero(size, size),
                 .vector = Eigen::VectorXd::Zero(size)};
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = a;
    nodes.x[1] = b;
    REQUIRE(element.compute(nodes, 0, out.matrix, out.vector).has_value());
    return out;
}

} // namespace

// REQ-008b-07, 23: without warping, the exact element is the 008 element,
// with offsets, connections and the shear centre.
TEST_CASE("the exact element without warping is the beam element", "[core][beam][warping]")
{
    const Eigen::Vector3d a(0.5, -1.0, 2.0);
    const Eigen::Vector3d b(3.0, 1.0, 2.5);
    BeamData beam = sampleBeam();
    beam.axes = axesOf(a, b);
    beam.endAxes = beam.axes;
    beam.offsets[0] = Eigen::Vector3d(0.1, 0.05, 0.0);
    beam.offsets[1] = Eigen::Vector3d(-0.05, 0.0, 0.08);
    beam.springs[1][5] = 0.0; // Mz released at the end
    beam.springs[0][4] = 3e4; // a spring on My at the start
    SpanLoads loads;
    fem::core::SpanPiece piece;
    piece.from = 0.3;
    piece.to = 2.0;
    piece.start << 1e2, -3e2, 2e2, 10.0;
    piece.end << 0.0, 5e2, -1e2, 0.0;
    loads.pieces = {piece};
    const double length = fem::core::flexibleLength(beam, a, b);
    beam.fixedEndLoads = fem::core::equivalentLoads(beam, length, loads);

    const std::vector<BeamData> beams = {beam};
    const fem::core::BeamElement reference(ElementKind::Beam3D, beams);
    Eigen::MatrixXd k = Eigen::MatrixXd::Zero(12, 12);
    Eigen::VectorXd f = Eigen::VectorXd::Zero(12);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line2;
    nodes.count = 2;
    nodes.x[0] = a;
    nodes.x[1] = b;
    REQUIRE(reference.compute(nodes, 0, k, f).has_value());

    const Computed e = exact(beam, loads, a, b, {false, false}, 12);
    CHECK((e.matrix - k).norm() <= 1e-10 * k.norm());
    CHECK((e.vector - f).norm() <= 1e-10 * f.norm());
}

// REQ-008b-07, 09: the warping dof of each end, by its condition.
TEST_CASE("the warping conditions of the exact element", "[core][beam][warping]")
{
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(3.0, 0.0, 0.0);
    BeamData beam = sampleBeam();
    beam.shearCentre.setZero();
    const double mu = 3.0;
    const double length = 3.0;
    beam.warpingStiffness = beam.torsional * length * length / (mu * mu);

    SECTION("continuous at both ends: 14 dofs and six rigid modes")
    {
        const Computed e = exact(beam, {}, a, b, {true, true}, 14);
        for (int mode = 0; mode < 6; ++mode) {
            Eigen::Vector3d t = Eigen::Vector3d::Zero();
            Eigen::Vector3d phi = Eigen::Vector3d::Zero();
            (mode < 3 ? t : phi)(mode % 3) = 1.0;
            Eigen::VectorXd d = Eigen::VectorXd::Zero(14);
            d.segment<3>(0) = t + phi.cross(a);
            d.segment<3>(3) = phi;
            d.segment<3>(7) = t + phi.cross(b);
            d.segment<3>(10) = phi;
            CHECK((e.matrix * d).norm() <= 1e-9 * e.matrix.norm());
        }
    }
    SECTION("free at both ends: uniform torsion GJ / L")
    {
        beam.warpingEnds = {WarpingCondition::Free, WarpingCondition::Free};
        const Computed e = exact(beam, {}, a, b, {false, false}, 12);
        CHECK(std::abs(e.matrix(3, 3) - beam.torsional / length) <=
              1e-10 * beam.torsional / length);
        CHECK(std::abs(e.matrix(3, 9) + beam.torsional / length) <=
              1e-10 * beam.torsional / length);
    }
    SECTION("restrained at both ends: GJ / L mu sinh mu / (mu sinh mu - 2 (cosh mu - 1))")
    {
        beam.warpingEnds = {WarpingCondition::Restrained, WarpingCondition::Restrained};
        const Computed e = exact(beam, {}, a, b, {false, false}, 12);
        const double expected = beam.torsional / length * mu * std::sinh(mu) /
                                (mu * std::sinh(mu) - 2.0 * (std::cosh(mu) - 1.0));
        CHECK(std::abs(e.matrix(3, 3) - expected) <= 1e-10 * expected);
    }
    SECTION("the dofs of the nodes")
    {
        const std::vector<BeamData> beams = {beam};
        const std::vector<SpanLoads> spans = {SpanLoads{}};
        const ExactBeamElement element(ElementKind::Beam3D, {true, false}, beams, spans);
        CHECK((element.dofsAt(fem::core::ElementType::Line2, 0) &
               fem::core::mask(fem::core::DofType::Warp)) != 0);
        CHECK((element.dofsAt(fem::core::ElementType::Line2, 1) &
               fem::core::mask(fem::core::DofType::Warp)) == 0);
        CHECK(element.dofsAt(fem::core::ElementType::Line3, 2) == 0);
    }
}

namespace {

// Cantilever of n collinear bars along x, the generic section with Iw.
fem::core::Model warpingCantilever(int n, double length, double iw)
{
    fem::core::test::TrussSpec spec;
    for (int i = 0; i <= n; ++i) {
        spec.nodes.emplace_back(length * i / n, 0.0, 0.0);
    }
    for (int i = 0; i < n; ++i) {
        spec.bars.push_back({i, i + 1});
    }
    fem::core::Model model = fem::core::test::beamModel(spec, ElementKind::Beam3D);
    model.sections.front().warpingConstant = iw;
    model.assignments.front().warping = true;
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.warpingEnds.push_back({.elements = fem::core::ElementIds{{1}},
                                 .end = fem::core::BarEnd::Start,
                                 .condition = WarpingCondition::Restrained});
    return model;
}

} // namespace

// REQ-008b-07, 09, 10, 18, 19: a cantilever with warping restrained at the
// root and a torque at the tip: theta(x) = T / (GJ k) (k x - sinh kx +
// tanh kL (cosh kx - 1)), B(0) = -T tanh(kL) / k, all the root torque
// carried by warping.
TEST_CASE("analysis of a cantilever with warping", "[core][beam][warping]")
{
    using fem::core::test::kG;
    using fem::core::test::kJ;
    const double length = 4.0;
    const double iw = 2.0e-7;
    const double gj = kG * kJ;
    const double k = std::sqrt(gj / (fem::core::test::kE * iw));
    const double torque = 1.0e4;
    // In the stable form: tanh kL (cosh kx - 1) - sinh kx =
    // sinh k(L - x) / cosh kL - tanh kL.
    const auto theta = [&](double x) {
        return torque / (gj * k) *
               (k * x - std::tanh(k * length) +
                std::sinh(k * (length - x)) / std::cosh(k * length));
    };
    const auto rate = [&](double x) {
        return torque / gj * (1.0 - std::cosh(k * (length - x)) / std::cosh(k * length));
    };
    for (const int n : {1, 4}) {
        INFO("bars " << n);
        fem::core::Model model = warpingCantilever(n, length, iw);
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{static_cast<fem::core::ExternalId>(n + 1)}},
             .moment = Eigen::Vector3d(torque, 0.0, 0.0)});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        const auto tip = static_cast<std::size_t>(n);
        CHECK(fem::core::test::near(result.rotations[tip].x(), theta(length), 1e-8));
        if (n == 4) {
            // The inner nodes share the warping of the collinear bars.
            CHECK(fem::core::test::near(result.warping[2], rate(length / 2), 1e-8));
        }
        const fem::core::BeamResult& root = fem::core::test::beamOf(result, 0);
        CHECK(fem::core::test::near(root.start().b, -torque * std::tanh(k * length) / k, 1e-8));
        CHECK(fem::core::test::near(root.start().tw, torque, 1e-8));
        CHECK(std::abs(root.start().tsv) <= 1e-8 * torque);
        const double x = 0.3 * root.length();
        const fem::core::BeamPoint p = root.at(x);
        CHECK(fem::core::test::near(p.rotation.x(), theta(x), 1e-8));
        CHECK(fem::core::test::near(p.warping, rate(x), 1e-8));
        CHECK(fem::core::test::near(p.forces.t, torque, 1e-8));
    }
}
