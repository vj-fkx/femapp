// REQ-008b-13, 23: the exact beam element (design 008b §5.3); its warping,
// kept in the core for the version with warping (008d), with free or
// restrained ends.

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
               const Eigen::Vector3d& b, int size)
{
    const std::vector<BeamData> beams = {beam};
    const std::vector<SpanLoads> spans = {loads};
    const ExactBeamElement element(ElementKind::Beam3D, beams, spans);
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

// REQ-008b-23: without warping nor curvature, the exact element is the 008 element,
// with offsets, connections and the shear centre.
TEST_CASE("the exact element without warping is the beam element", "[core][beam][exact]")
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

    const Computed e = exact(beam, loads, a, b, 12);
    CHECK((e.matrix - k).norm() <= 1e-10 * k.norm());
    CHECK((e.vector - f).norm() <= 1e-10 * f.norm());
}

// The warping of the exact bar, by the condition of its ends (core only).
TEST_CASE("the warping conditions of the exact element", "[core][beam][exact]")
{
    const Eigen::Vector3d a(0.0, 0.0, 0.0);
    const Eigen::Vector3d b(3.0, 0.0, 0.0);
    BeamData beam = sampleBeam();
    beam.shearCentre.setZero();
    const double mu = 3.0;
    const double length = 3.0;
    beam.warpingStiffness = beam.torsional * length * length / (mu * mu);

    SECTION("free at both ends: uniform torsion GJ / L")
    {
        beam.warpingEnds = {WarpingCondition::Free, WarpingCondition::Free};
        const Computed e = exact(beam, {}, a, b, 12);
        CHECK(std::abs(e.matrix(3, 3) - beam.torsional / length) <=
              1e-10 * beam.torsional / length);
        CHECK(std::abs(e.matrix(3, 9) + beam.torsional / length) <=
              1e-10 * beam.torsional / length);
    }
    SECTION("restrained at both ends: GJ / L mu sinh mu / (mu sinh mu - 2 (cosh mu - 1))")
    {
        beam.warpingEnds = {WarpingCondition::Restrained, WarpingCondition::Restrained};
        const Computed e = exact(beam, {}, a, b, 12);
        const double expected = beam.torsional / length * mu * std::sinh(mu) /
                                (mu * std::sinh(mu) - 2.0 * (std::cosh(mu) - 1.0));
        CHECK(std::abs(e.matrix(3, 3) - expected) <= 1e-10 * expected);
    }
    SECTION("the dofs of the nodes: no warping dof, none at the middle of an arc")
    {
        const std::vector<BeamData> beams = {beam};
        const std::vector<SpanLoads> spans = {SpanLoads{}};
        const ExactBeamElement element(ElementKind::Beam3D, beams, spans);
        using fem::core::DofType;
        using fem::core::mask;
        CHECK(element.dofsAt(fem::core::ElementType::Line2, 1) ==
              (mask(DofType::U) | mask(DofType::V) | mask(DofType::W) | mask(DofType::RX) |
               mask(DofType::RY) | mask(DofType::RZ)));
        CHECK(element.dofsAt(fem::core::ElementType::Line3, 2) == 0);
    }
}
