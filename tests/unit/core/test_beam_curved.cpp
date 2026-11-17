// REQ-008b-12 to 16: curved beams (design 008b §5.2, 5.3).

#include "beam_models.hpp"

#include <fem/core/beam_element.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

using fem::core::Arc;
using fem::core::arcThrough;
using Vec = Eigen::Vector3d;

namespace {

constexpr double kPi = std::numbers::pi;

Arc arcOf(const Vec& p, const Vec& m, const Vec& q)
{
    const auto arc = arcThrough(p, m, q);
    REQUIRE(arc.has_value());
    return arc.value_or(Arc{});
}

std::string failure(const Vec& p, const Vec& m, const Vec& q)
{
    const auto arc = arcThrough(p, m, q);
    REQUIRE_FALSE(arc.has_value());
    return arc.error().message;
}

} // namespace

// REQ-008b-13, 16: the circle through the three nodes.
TEST_CASE("the arc through three nodes", "[core][beam][curved]")
{
    const double r = 2.5;
    SECTION("a quarter of a circle")
    {
        const Arc arc =
            arcOf(Vec(r, 0, 0), Vec(r, 0, 0).norm() * Vec(std::cos(kPi / 4), std::sin(kPi / 4), 0),
                  Vec(0, r, 0));
        CHECK(std::abs(arc.radius - r) <= 1e-14 * r);
        CHECK(arc.centre.norm() <= 1e-14 * r);
        CHECK(std::abs(arc.length - kPi * r / 2) <= 1e-13 * r);
        CHECK((arc.normal - Vec::UnitZ()).norm() <= 1e-14);
        CHECK((arc.startTangent - Vec::UnitY()).norm() <= 1e-14);
        CHECK((arc.endTangent + Vec::UnitX()).norm() <= 1e-14);
    }
    SECTION("three quarters, the middle node beyond the half")
    {
        const Arc arc = arcOf(Vec(r, 0, 0), Vec(-r, 0, 0), Vec(0, -r, 0));
        CHECK(std::abs(arc.length - 1.5 * kPi * r) <= 1e-13 * r);
    }
    SECTION("a tilted half circle")
    {
        const Eigen::Matrix3d turn =
            Eigen::AngleAxisd(0.7, Vec(1, 2, 3).normalized()).toRotationMatrix();
        const Vec c(1.0, -2.0, 0.5);
        const Arc arc =
            arcOf(c + turn * Vec(r, 0, 0), c + turn * Vec(0, r, 0), c + turn * Vec(-r, 0, 0));
        CHECK((arc.centre - c).norm() <= 1e-13 * r);
        CHECK(std::abs(arc.length - kPi * r) <= 1e-13 * r);
        CHECK((arc.normal - turn * Vec::UnitZ()).norm() <= 1e-13);
    }
    SECTION("collinear nodes: a straight bar")
    {
        const Arc arc = arcOf(Vec(0, 0, 0), Vec(1, 1, 0), Vec(3, 3, 0));
        CHECK(arc.radius == 0.0);
        CHECK(std::abs(arc.length - 3 * std::sqrt(2.0)) <= 1e-14);
        CHECK((arc.startTangent - Vec(1, 1, 0).normalized()).norm() <= 1e-15);
    }
    SECTION("refused")
    {
        CHECK(failure(Vec(0, 0, 0), Vec(0, 0, 0), Vec(1, 0, 0)) ==
              "the middle node coincides with an end node.");
        CHECK(failure(Vec(0, 0, 0), Vec(1, 0, 0), Vec(0, 0, 0)) == "the arc closes on itself.");
        CHECK(failure(Vec(0, 0, 0), Vec(3, 0, 0), Vec(1, 0, 0)) ==
              "the middle node is not between the end nodes.");
    }
}

// REQ-008b-15: the local axes turn with the arc; the curvature can have any
// direction in the section.
TEST_CASE("the axes along an arc", "[core][beam][curved]")
{
    const double r = 4.0;
    const auto tangentAt = [](const Arc& arc, double s) {
        return Eigen::AngleAxisd(s / arc.radius, arc.normal) * arc.startTangent;
    };

    SECTION("a horizontal arc with the default reference: y stays vertical")
    {
        const Arc arc = arcOf(Vec(r, 0, 0), Vec(r * std::cos(0.4), r * std::sin(0.4), 0),
                              Vec(r * std::cos(1.2), r * std::sin(1.2), 0));
        const auto start = fem::core::beamAxes(Vec(r, 0, 0), Vec(r, 0, 0) + arc.startTangent,
                                               std::nullopt, 0.3, fem::core::ElementKind::Beam3D);
        REQUIRE(start.has_value());
        const Eigen::Matrix3d axes = start.value_or(Eigen::Matrix3d::Identity());
        const Vec curvature = fem::core::arcCurvature(axes, arc);
        CHECK(std::abs(curvature.x()) <= 1e-15);
        CHECK(std::abs(curvature.norm() - 1 / r) <= 1e-15);
        for (const double s : {0.0, 1.0, arc.length}) {
            const Eigen::Matrix3d at = fem::core::arcAxesAt(axes, curvature, s);
            CHECK((at.col(0) - tangentAt(arc, s)).norm() <= 1e-14);
            CHECK(std::abs(at.col(1).dot(Vec::UnitZ()) - axes.col(1).dot(Vec::UnitZ())) <= 1e-14);
            CHECK((at.transpose() * at - Eigen::Matrix3d::Identity()).norm() <= 1e-14);
        }
        CHECK((fem::core::arcAxesAt(axes, curvature, arc.length).col(0) - arc.endTangent).norm() <=
              1e-14);
    }
    SECTION("a vertical half circle: the default reference at a vertical start")
    {
        const Arc arc = arcOf(Vec(-r, 0, 0), Vec(0, 0, r), Vec(r, 0, 0));
        const auto start = fem::core::beamAxes(Vec(-r, 0, 0), Vec(-r, 0, 0) + arc.startTangent,
                                               std::nullopt, 0.0, fem::core::ElementKind::Beam3D);
        REQUIRE(start.has_value());
        const Eigen::Matrix3d axes = start.value_or(Eigen::Matrix3d::Identity());
        const Vec curvature = fem::core::arcCurvature(axes, arc);
        const Eigen::Matrix3d top = fem::core::arcAxesAt(axes, curvature, arc.length / 2);
        CHECK((top.col(0) - Vec::UnitX()).norm() <= 1e-14);
        // The local y stays in the plane of the arch, toward its centre at the top.
        CHECK((top.col(1) + Vec::UnitZ()).norm() <= 1e-14);
    }
}

namespace {

// One beam from (0, 0, 0) to `end`, as a Line2 or as a Line3 with the
// middle node at `middle`; the generic section, steel, fixed at node 1.
fem::core::Model oneBeam(const Vec& end, const std::optional<Vec>& middle)
{
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec::Zero()).has_value());
    REQUIRE(builder.addNode(2, end).has_value());
    if (middle) {
        REQUIRE(builder.addNode(3, *middle).has_value());
        const std::vector<fem::core::ExternalId> line = {1, 2, 3};
        REQUIRE(builder.addElement(fem::core::ElementType::Line3, 1, line).has_value());
    } else {
        const std::vector<fem::core::ExternalId> line = {1, 2};
        REQUIRE(builder.addElement(fem::core::ElementType::Line2, 1, line).has_value());
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    model.sections.front().shearCentre = Eigen::Vector2d(0.01, -0.02);
    model.assignments.push_back({.elements = fem::core::ElementIds{{1}},
                                 .kind = fem::core::ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "S",
                                 .theory = fem::core::BeamTheory::Timoshenko});
    fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
    return model;
}

} // namespace

// REQ-008b-12, 13, 15, 23: a Line3 on a straight line is the straight beam,
// with global and local span loads, a concentrated load and the weight.
TEST_CASE("a straight 3-node beam is the 2-node beam", "[core][beam][curved]")
{
    const Vec end(3.0, 1.0, -2.0);
    const auto load = [](fem::core::Model& model) {
        model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                          .axes = fem::core::Axes::Global,
                                          .startValue = Vec(0.0, 2e3, -5e3),
                                          .endValue = Vec(1e3, 0.0, -1e3),
                                          .from = {.value = 0.2, .fraction = true},
                                          .to = {.value = 0.9, .fraction = true},
                                          .startTorque = 300.0,
                                          .endTorque = 100.0});
        model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                           .axes = fem::core::Axes::Global,
                                           .force = Vec(4e3, -1e3, 2e3),
                                           .moment = Vec(0.0, 500.0, 0.0),
                                           .at = {.value = 0.35, .fraction = true}});
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{2}}, .force = Vec(1e3, 1e3, 1e3)});
        model.gravity = Vec(0.0, 0.0, -9.81);
    };
    fem::core::Model straight = oneBeam(end, std::nullopt);
    fem::core::Model curved = oneBeam(end, Vec(0.4 * end));
    load(straight);
    load(curved);
    const fem::core::StaticResult a = fem::core::test::solve(straight);
    const fem::core::StaticResult b = fem::core::test::solve(curved);
    CHECK((b.displacements[1] - a.displacements[1]).norm() <= 1e-10 * a.displacements[1].norm());
    CHECK((b.rotations[1] - a.rotations[1]).norm() <= 1e-10 * a.rotations[1].norm());
    CHECK((b.reactions[0] - a.reactions[0]).norm() <= 1e-10 * a.reactions[0].norm());
    // The middle node of the Line3 has no dofs.
    CHECK(b.displacements[2] == Vec::Zero());
    const fem::core::BeamResult& r = fem::core::test::beamOf(b, 0);
    const fem::core::BeamResult& s = fem::core::test::beamOf(a, 0);
    for (const double x : {0.0, 1.1, 2.3, r.length()}) {
        const auto p = r.at(x);
        const auto q = s.at(x);
        CHECK(std::abs(p.forces.mz - q.forces.mz) <= 1e-9 * (std::abs(q.forces.mz) + 1e3));
        CHECK(std::abs(p.forces.t - q.forces.t) <= 1e-9 * (std::abs(q.forces.t) + 1e3));
        CHECK((p.displacement - q.displacement).norm() <= 1e-10 * (q.displacement.norm() + 1e-3));
    }
}
