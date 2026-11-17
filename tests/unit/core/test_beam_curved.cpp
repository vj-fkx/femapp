// REQ-008b-12 to 16: curved beams (design 008b §5.2, 5.3).

#include "arc_reference.hpp"
#include "beam_models.hpp"

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_iso.hpp>
#include <fem/core/mesh_builder.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <array>
#include <cmath>
#include <functional>
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

namespace {

using fem::core::test::ArcReference;
using fem::core::test::Internal;

constexpr double kTolerance = 1e-8; // NFR-008b-01

fem::core::test::ArcSection genericArcSection()
{
    using namespace fem::core::test;
    return {.ea = kE * kArea,
            .gay = kG * kShearArea,
            .gaz = kG * kShearArea,
            .gj = kG * kJ,
            .eiy = kE * kIy,
            .eiz = kE * kIz};
}

// Arcs of radius r in the XY plane, one Line3 between consecutive angles
// (the end nodes numbered 1, 2, ... in order, the middle ones from 1001);
// `closed` joins the last angle to the first node. Steel, the generic
// section, Timoshenko.
fem::core::Model arcModel(double r, const std::vector<double>& angles, fem::core::ElementKind kind,
                          bool closed = false)
{
    fem::core::MeshBuilder builder;
    const auto point = [r](double theta) {
        return Vec(r * std::cos(theta), r * std::sin(theta), 0.0);
    };
    const std::size_t ends = closed ? angles.size() - 1 : angles.size();
    for (std::size_t i = 0; i < ends; ++i) {
        REQUIRE(builder.addNode(static_cast<fem::core::ExternalId>(i + 1), point(angles[i]))
                    .has_value());
    }
    // The nodes first, then the elements.
    for (std::size_t i = 0; i + 1 < angles.size(); ++i) {
        REQUIRE(builder
                    .addNode(static_cast<fem::core::ExternalId>(1001 + i),
                             point((angles[i] + angles[i + 1]) / 2))
                    .has_value());
    }
    fem::core::ElementIds all;
    for (std::size_t i = 0; i + 1 < angles.size(); ++i) {
        const auto middle = static_cast<fem::core::ExternalId>(1001 + i);
        const auto next =
            static_cast<fem::core::ExternalId>(closed && i + 2 == angles.size() ? 1 : i + 2);
        const std::vector<fem::core::ExternalId> line = {static_cast<fem::core::ExternalId>(i + 1),
                                                         next, middle};
        const auto id = static_cast<fem::core::ExternalId>(i + 1);
        REQUIRE(builder.addElement(fem::core::ElementType::Line3, id, line).has_value());
        all.ids.push_back(id);
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    model.assignments.push_back({.elements = all,
                                 .kind = kind,
                                 .material = "Steel",
                                 .section = "S",
                                 .theory = fem::core::BeamTheory::Timoshenko});
    return model;
}

bool close(const Vec& value, const Vec& reference, double scale)
{
    return (value - reference).norm() <= kTolerance * scale;
}

} // namespace

// REQ-008b-13, 15, 19, NFR-008b-01: a quarter ring in cantilever with a
// force and a couple at the tip, in and out of its plane (bending, torsion
// and normal force coupled by the curvature).
TEST_CASE("quarter ring in cantilever", "[core][beam][curved][verification]")
{
    const double r = 2.0;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = false};
    const Vec force(2e3, -3e3, 1.5e3);
    const Vec couple(100.0, -200.0, 300.0);
    const double tip = std::numbers::pi / 2;
    const auto real = [&](double theta) { return reference.fromPoint(theta, tip, force, couple); };
    Vec displacement;
    Vec rotation;
    for (int i = 0; i < 3; ++i) {
        const auto unitForce = [&](double theta) {
            return reference.fromPoint(theta, tip, Vec::Unit(i), Vec::Zero());
        };
        const auto unitCouple = [&](double theta) {
            return reference.fromPoint(theta, tip, Vec::Zero(), Vec::Unit(i));
        };
        displacement(i) = reference.work(0.0, tip, real, unitForce);
        rotation(i) = reference.work(0.0, tip, real, unitCouple);
    }
    for (const std::vector<double>& angles :
         {std::vector<double>{0.0, tip}, std::vector<double>{0.0, 0.6, tip}}) {
        fem::core::Model model = arcModel(r, angles, fem::core::ElementKind::Beam3D);
        fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
        const auto end = static_cast<fem::core::ExternalId>(angles.size());
        model.nodalForces.push_back(
            {.nodes = fem::core::NodeIds{{end}}, .force = force, .moment = couple});
        const fem::core::StaticResult result = fem::core::test::solve(model);
        const auto node = static_cast<std::size_t>(end - 1);
        CHECK(close(result.displacements[node], displacement, displacement.norm()));
        CHECK(close(result.rotations[node], rotation, rotation.norm()));
        // The internal forces along the first arc.
        const fem::core::BeamResult& beam = fem::core::test::beamOf(result, 0);
        for (const double s : {0.0, 0.37, beam.length()}) {
            const fem::core::BeamForces f = beam.at(s).forces;
            Internal at;
            at << f.n, f.vy, f.vz, f.t, f.my, f.mz;
            const Internal expected = real(s / r);
            CHECK((at - expected).norm() <= kTolerance * expected.norm());
        }
    }
}

// REQ-008b-13, 15, NFR-008b-01: a two-hinged half circle arch (2D) under a
// uniform load per length of arc fixed in space, and under a uniform
// temperature change: the thrust and the deflection of the crown.
TEST_CASE("two-hinged half circle arch", "[core][beam][curved][verification]")
{
    const double r = 5.0;
    const double pi = std::numbers::pi;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = true};
    const Vec q(0.0, -4e3, 0.0);
    // The determinate base: hinge at theta = 0, roller (vertical) at pi.
    const auto rule = fem::core::test::gaussLegendre(64);
    double loadMoment = 0.0; // of the load about the hinge
    for (const auto& [x, w] : rule) {
        const double theta = pi / 2 + pi / 2 * x;
        loadMoment += w * (reference.point(theta) - reference.point(0.0)).cross(q).z() * r * pi / 2;
    }
    const Vec a = reference.point(0.0);
    const Vec b = reference.point(pi);
    const double roller = -loadMoment / (b - a).x();
    const auto base = [&](double theta) {
        return Internal(reference.fromUniform(theta, pi, q) +
                        reference.fromPoint(theta, pi, Vec(0.0, roller, 0.0), Vec::Zero()));
    };
    const auto thrust = [&](double theta) {
        return reference.fromPoint(theta, pi, Vec::UnitX(), Vec::Zero());
    };
    const double flexibility = reference.work(0.0, pi, thrust, thrust);
    // A unit vertical force at the crown on the base.
    const double crownRoller = -(reference.point(pi / 2) - a).x() / (b - a).x();
    const auto crown = [&](double theta) {
        Internal x = reference.fromPoint(theta, pi, Vec(0.0, crownRoller, 0.0), Vec::Zero());
        if (theta < pi / 2) {
            x += reference.fromPoint(theta, pi / 2, Vec::UnitY(), Vec::Zero());
        }
        return x;
    };
    const auto model = [&] {
        fem::core::Model m = arcModel(r, {0.0, pi / 2, pi}, fem::core::ElementKind::Beam2D);
        fem::core::test::restrain(m, {1, 3}, {fem::core::DofType::U, fem::core::DofType::V});
        return m;
    };

    SECTION("uniform load")
    {
        const double h = -reference.work(0.0, pi, base, thrust) / flexibility;
        const auto total = [&](double theta) { return Internal(base(theta) + h * thrust(theta)); };
        const double deflection =
            reference.work(0.0, pi / 2, total, crown) + reference.work(pi / 2, pi, total, crown);
        fem::core::Model m = model();
        m.distributedLoads.push_back({.elements = fem::core::ElementIds{{1, 2}},
                                      .axes = fem::core::Axes::Global,
                                      .startValue = q,
                                      .endValue = q});
        const fem::core::StaticResult result = fem::core::test::solve(m);
        CHECK(fem::core::test::near(result.reactions[2].x(), h, kTolerance));
        CHECK(fem::core::test::near(result.displacements[1].y(), deflection, kTolerance));
    }
    SECTION("uniform temperature")
    {
        const double deltaT = 40.0;
        const double alpha = fem::core::test::steel().thermalExpansion.value_or(0.0);
        const double h = -alpha * deltaT * (b - a).x() / flexibility;
        const auto total = [&](double theta) { return Internal(h * thrust(theta)); };
        const double rise = reference.work(0.0, pi / 2, total, crown) +
                            reference.work(pi / 2, pi, total, crown) + alpha * deltaT * r;
        fem::core::Model m = model();
        m.temperatureChanges.push_back(
            {.elements = fem::core::ElementIds{{1, 2}}, .deltaT = deltaT});
        const fem::core::StaticResult result = fem::core::test::solve(m);
        CHECK(fem::core::test::near(result.reactions[2].x(), h, kTolerance));
        CHECK(fem::core::test::near(result.displacements[1].y(), rise, kTolerance));
    }
}

// REQ-008b-13, NFR-008b-01: a ring pressed by two opposite forces along a
// diameter (2D): the shortening of that diameter.
TEST_CASE("ring under a diametral load", "[core][beam][curved][verification]")
{
    const double r = 1.5;
    const double pi = std::numbers::pi;
    const double p = 5e4;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = true};
    // Half of the ring, fixed at the bottom by symmetry; at the top, half of
    // the force, and a thrust and a moment such that the top does not move
    // sideways nor turn.
    const double bottom = -pi / 2;
    const double top = pi / 2;
    const auto load = [&](double theta) {
        return reference.fromPoint(theta, top, Vec(0.0, -p / 2, 0.0), Vec::Zero());
    };
    const auto sideways = [&](double theta) {
        return reference.fromPoint(theta, top, Vec::UnitX(), Vec::Zero());
    };
    const auto turn = [&](double theta) {
        return reference.fromPoint(theta, top, Vec::Zero(), Vec::UnitZ());
    };
    Eigen::Matrix2d f;
    f << reference.work(bottom, top, sideways, sideways),
        reference.work(bottom, top, sideways, turn), reference.work(bottom, top, turn, sideways),
        reference.work(bottom, top, turn, turn);
    const Eigen::Vector2d d(reference.work(bottom, top, load, sideways),
                            reference.work(bottom, top, load, turn));
    const Eigen::Vector2d redundant = f.inverse() * -d;
    const auto total = [&](double theta) {
        return Internal(load(theta) + redundant(0) * sideways(theta) + redundant(1) * turn(theta));
    };
    const auto down = [&](double theta) {
        return reference.fromPoint(theta, top, Vec::UnitY(), Vec::Zero());
    };
    const double shortening = reference.work(bottom, top, total, down);

    fem::core::Model model =
        arcModel(r, {0.0, pi / 2, pi, 3 * pi / 2, 2 * pi}, fem::core::ElementKind::Beam2D, true);
    fem::core::test::restrain(model, {2}, {fem::core::DofType::U});
    fem::core::test::restrain(model, {4}, {fem::core::DofType::U, fem::core::DofType::V});
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0.0, -p, 0.0)});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    CHECK(fem::core::test::near(result.displacements[1].y(), shortening, kTolerance));
}

// REQ-008b-13, 15, NFR-008b-01: a curved beam in plan (a quarter of a
// circle), fixed at both ends, under a uniform vertical load: bending and
// torsion together.
TEST_CASE("curved beam in plan fixed at both ends", "[core][beam][curved][verification]")
{
    const double r = 3.0;
    const double end = std::numbers::pi / 2;
    const double middle = end / 2;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = false};
    const Vec q(0.0, 0.0, -6e3);
    // The cantilever from theta = 0, with the out-of-plane redundants at the
    // other end (the in-plane ones are zero: the load does not reach them).
    const auto base = [&](double theta) { return reference.fromUniform(theta, end, q); };
    const std::array<std::function<Internal(double)>, 3> unit = {
        [&](double theta) { return reference.fromPoint(theta, end, Vec::UnitZ(), Vec::Zero()); },
        [&](double theta) { return reference.fromPoint(theta, end, Vec::Zero(), Vec::UnitX()); },
        [&](double theta) { return reference.fromPoint(theta, end, Vec::Zero(), Vec::UnitY()); }};
    Eigen::Matrix3d f;
    Eigen::Vector3d d;
    for (std::size_t i = 0; i < 3; ++i) {
        d(static_cast<Eigen::Index>(i)) = reference.work(0.0, end, base, unit.at(i));
        for (std::size_t j = 0; j < 3; ++j) {
            f(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) =
                reference.work(0.0, end, unit.at(i), unit.at(j));
        }
    }
    const Eigen::Vector3d redundant = f.inverse() * -d;
    const auto total = [&](double theta) {
        Internal x = base(theta);
        for (std::size_t i = 0; i < 3; ++i) {
            x += redundant(static_cast<Eigen::Index>(i)) * unit.at(i)(theta);
        }
        return x;
    };
    const auto atMiddle = [&](double theta) {
        return reference.fromPoint(theta, middle, Vec::UnitZ(), Vec::Zero());
    };
    const double deflection = reference.work(0.0, middle, total, atMiddle);

    fem::core::Model model = arcModel(r, {0.0, middle, end}, fem::core::ElementKind::Beam3D);
    fem::core::test::fixAll(model, {1, 3}, fem::core::ElementKind::Beam3D);
    model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1, 2}},
                                      .axes = fem::core::Axes::Global,
                                      .startValue = q,
                                      .endValue = q});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    CHECK(fem::core::test::near(result.displacements[1].z(), deflection, kTolerance));
    CHECK(fem::core::test::near(result.reactions[2].z(), redundant(0), kTolerance));
    CHECK(close(result.reactionMoments[2], Vec(redundant(1), redundant(2), 0.0),
                std::abs(redundant(1)) + std::abs(redundant(2))));
}

// REQ-008b-15, AC-8: the end connections of the 008 on arcs: moment
// releases at the supports of an arch give the hinges; rotational springs
// tend to the hinge and to the fixed end.
TEST_CASE("connections at the ends of arcs", "[core][beam][curved][verification]")
{
    const double r = 5.0;
    const double pi = std::numbers::pi;
    const Vec q(0.0, -4e3, 0.0);
    const auto arch = [&](std::optional<double> spring, bool fixed) {
        fem::core::Model m = arcModel(r, {0.0, pi / 2, pi}, fem::core::ElementKind::Beam2D);
        fem::core::test::restrain(m, {1, 3}, {fem::core::DofType::U, fem::core::DofType::V});
        if (fixed) {
            fem::core::test::restrain(m, {1, 3}, {fem::core::DofType::RZ});
        }
        if (spring) {
            fem::core::EndConnection start{.elements = fem::core::ElementIds{{1}},
                                           .end = fem::core::BarEnd::Start};
            start.stiffness[5] = *spring;
            fem::core::EndConnection end{.elements = fem::core::ElementIds{{2}},
                                         .end = fem::core::BarEnd::End};
            end.stiffness[5] = *spring;
            m.endConnections = {start, end};
        }
        m.distributedLoads.push_back({.elements = fem::core::ElementIds{{1, 2}},
                                      .axes = fem::core::Axes::Global,
                                      .startValue = q,
                                      .endValue = q});
        return fem::core::test::solve(m);
    };
    const fem::core::StaticResult hinged = arch(std::nullopt, false);
    const fem::core::StaticResult fixedEnds = arch(std::nullopt, true);
    const double ei = fem::core::test::kE * fem::core::test::kIz;
    CHECK(fem::core::test::near(arch(0.0, true).reactions[2].x(), hinged.reactions[2].x(), 1e-10));
    CHECK(fem::core::test::near(arch(1e-6 * ei / r, true).displacements[1].y(),
                                hinged.displacements[1].y(), 1e-5));
    CHECK(fem::core::test::near(arch(1e9 * ei / r, true).displacements[1].y(),
                                fixedEnds.displacements[1].y(), 1e-6));
}

// REQ-008b-15, AC-8: a rigid offset at the start of an arc: the same arc
// from the offset end, and the reaction moment moved by the lever.
TEST_CASE("rigid offsets on arcs", "[core][beam][curved][verification]")
{
    const double r = 2.0;
    const double tip = std::numbers::pi / 2;
    const Vec offset(0.15, -0.1, 0.05);
    const Vec force(2e3, -3e3, 1.5e3);
    fem::core::Model plain = arcModel(r, {0.0, tip}, fem::core::ElementKind::Beam3D);
    fem::core::test::fixAll(plain, {1}, fem::core::ElementKind::Beam3D);
    plain.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = force});
    // The node moved back by the offset, which brings the arc where it was.
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec(r, 0.0, 0.0) - offset).has_value());
    REQUIRE(builder.addNode(2, Vec(0.0, r, 0.0)).has_value());
    REQUIRE(builder.addNode(3, Vec(r * std::cos(tip / 2), r * std::sin(tip / 2), 0.0)).has_value());
    const std::vector<fem::core::ExternalId> line = {1, 2, 3};
    REQUIRE(builder.addElement(fem::core::ElementType::Line3, 1, line).has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model offsetModel = plain;
    offsetModel.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    offsetModel.rigidOffsets.push_back({.elements = fem::core::ElementIds{{1}}, .start = offset});
    const fem::core::StaticResult a = fem::core::test::solve(plain);
    const fem::core::StaticResult b = fem::core::test::solve(offsetModel);
    CHECK(close(b.displacements[1], a.displacements[1], a.displacements[1].norm()));
    CHECK(close(b.rotations[1], a.rotations[1], a.rotations[1].norm()));
    CHECK(close(b.reactions[0], a.reactions[0], a.reactions[0].norm()));
    CHECK(close(b.reactionMoments[0], a.reactionMoments[0] + offset.cross(a.reactions[0]),
                a.reactionMoments[0].norm()));
}

// REQ-008b-15, 17, AC-8: span loads along the arc length, in global and in
// local axes, partial and concentrated, with a distributed torque; and the
// weight as a global load.
TEST_CASE("span loads on arcs", "[core][beam][curved][verification]")
{
    const double r = 2.0;
    const double tip = std::numbers::pi / 2;
    const double length = r * tip;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = false};
    const Vec global(0.0, 1e3, -2e3);         // on [0.25, 0.75] of the arc
    const Vec local(-300.0, 500.0, 400.0);    // the whole arc, local axes
    const double torque = 150.0;              // about the local x, on [0.1, 0.6]
    const Vec point(1e3, -500.0, 2e3);        // global, at 0.3
    const Vec pointLocal(0.0, -800.0, 600.0); // local, at 0.7
    const auto angle = [&](double fraction) { return fraction * tip; };
    const auto inside = [&](double theta, double from, double to) {
        return theta >= angle(from) && theta <= angle(to);
    };
    const auto q = [&](double theta) {
        Vec value = reference.axes(theta) * local;
        if (inside(theta, 0.25, 0.75)) {
            value += global;
        }
        return value;
    };
    const auto c = [&](double theta) {
        return inside(theta, 0.1, 0.6) ? Vec(torque * reference.axes(theta).col(0)) : Vec::Zero();
    };
    const auto real = [&](double theta) {
        // The load between its breaks, then the concentrated loads.
        Internal x = Internal::Zero();
        double from = 0.0;
        for (const double fraction : {0.1, 0.25, 0.6, 0.75, 1.0}) {
            const double to = angle(fraction);
            x += reference.fromLoad(theta, std::max(from, theta), to, q, c);
            from = to;
        }
        if (theta < angle(0.3)) {
            x += reference.fromPoint(theta, angle(0.3), point, Vec::Zero());
        }
        if (theta < angle(0.7)) {
            x += reference.fromPoint(theta, angle(0.7), reference.axes(angle(0.7)) * pointLocal,
                                     Vec::Zero());
        }
        return x;
    };
    Vec displacement;
    for (int i = 0; i < 3; ++i) {
        const auto unit = [&](double theta) {
            return reference.fromPoint(theta, tip, Vec::Unit(i), Vec::Zero());
        };
        displacement(i) = 0.0;
        double from = 0.0;
        for (const double fraction : {0.1, 0.25, 0.3, 0.6, 0.7, 0.75, 1.0}) {
            displacement(i) += reference.work(from, angle(fraction), real, unit);
            from = angle(fraction);
        }
    }

    fem::core::Model model = arcModel(r, {0.0, tip}, fem::core::ElementKind::Beam3D);
    fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
    const fem::core::ElementIds arc{{1}};
    model.distributedLoads.push_back({.elements = arc,
                                      .axes = fem::core::Axes::Global,
                                      .startValue = global,
                                      .endValue = global,
                                      .from = {.value = 0.25, .fraction = true},
                                      .to = {.value = 0.75 * length, .fraction = false}});
    model.distributedLoads.push_back(
        {.elements = arc, .axes = fem::core::Axes::Local, .startValue = local, .endValue = local});
    model.distributedLoads.push_back({.elements = arc,
                                      .axes = fem::core::Axes::Local,
                                      .from = {.value = 0.1, .fraction = true},
                                      .to = {.value = 0.6, .fraction = true},
                                      .startTorque = torque,
                                      .endTorque = torque});
    model.concentratedLoads.push_back({.elements = arc,
                                       .axes = fem::core::Axes::Global,
                                       .force = point,
                                       .at = {.value = 0.3, .fraction = true}});
    model.concentratedLoads.push_back({.elements = arc,
                                       .axes = fem::core::Axes::Local,
                                       .force = pointLocal,
                                       .at = {.value = 0.7, .fraction = true}});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    CHECK(close(result.displacements[1], displacement, displacement.norm()));
    const fem::core::BeamResult& beam = fem::core::test::beamOf(result, 0);
    for (const double fraction : {0.05, 0.45, 0.9}) {
        const fem::core::BeamForces f = beam.at(fraction * length).forces;
        Internal at;
        at << f.n, f.vy, f.vz, f.t, f.my, f.mz;
        const Internal expected = real(angle(fraction));
        CHECK((at - expected).norm() <= kTolerance * expected.norm());
    }

    SECTION("the weight is a global load per length of arc")
    {
        fem::core::Model heavy = arcModel(r, {0.0, tip}, fem::core::ElementKind::Beam3D);
        fem::core::test::fixAll(heavy, {1}, fem::core::ElementKind::Beam3D);
        fem::core::Model loaded = heavy;
        heavy.gravity = Vec(0.0, 0.0, -9.81);
        const double weight =
            fem::core::test::steel().density.value_or(0.0) * fem::core::test::kArea * 9.81;
        loaded.distributedLoads.push_back({.elements = arc,
                                           .axes = fem::core::Axes::Global,
                                           .startValue = Vec(0.0, 0.0, -weight),
                                           .endValue = Vec(0.0, 0.0, -weight)});
        const fem::core::StaticResult a = fem::core::test::solve(heavy);
        const fem::core::StaticResult b = fem::core::test::solve(loaded);
        CHECK(close(a.displacements[1], b.displacements[1], b.displacements[1].norm()));
    }
}

// REQ-008b-15, AC-8: a temperature gradient across an arc in cantilever:
// the tip moves by the virtual work of the imposed curvatures.
TEST_CASE("temperature gradient on an arc", "[core][beam][curved][verification]")
{
    const double r = 2.0;
    const double tip = std::numbers::pi / 2;
    const ArcReference reference{.r = r, .section = genericArcSection(), .planar = false};
    const double alpha = fem::core::test::steel().thermalExpansion.value_or(0.0);
    const double alongY = 30.0;
    const double alongZ = -20.0;
    // The imposed curvatures of design 008 (sigma = E alpha (gy y + gz z)).
    const double curvatureY = alpha * alongZ;
    const double curvatureZ = -alpha * alongY;
    Vec displacement;
    for (int i = 0; i < 3; ++i) {
        const auto unit = [&](double theta) {
            return reference.fromPoint(theta, tip, Vec::Unit(i), Vec::Zero());
        };
        const auto curvature = [&](double /*theta*/) {
            Internal k = Internal::Zero();
            k(4) = curvatureY * reference.section.eiy;
            k(5) = curvatureZ * reference.section.eiz;
            return k;
        };
        displacement(i) = reference.work(0.0, tip, curvature, unit);
    }
    fem::core::Model model = arcModel(r, {0.0, tip}, fem::core::ElementKind::Beam3D);
    fem::core::test::fixAll(model, {1}, fem::core::ElementKind::Beam3D);
    model.temperatureGradients.push_back(
        {.elements = fem::core::ElementIds{{1}}, .alongY = alongY, .alongZ = alongZ});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    CHECK(close(result.displacements[1], displacement, displacement.norm()));
}

// REQ-008b-13, 15, AC-8: the exact element takes the elastic foundation
// exactly: a long straight 3-node beam is Hetenyi's beam, w = P beta / 2k
// under a concentrated load.
TEST_CASE("elastic foundation on the exact element", "[core][beam][curved][verification]")
{
    const double k = 2e6;
    const double ei = fem::core::test::kE * fem::core::test::kIz;
    const double beta = std::pow(k / (4 * ei), 0.25);
    const double half = 20.0 / beta;
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec(-half, 0.0, 0.0)).has_value());
    REQUIRE(builder.addNode(2, Vec(0.0, 0.0, 0.0)).has_value());
    REQUIRE(builder.addNode(3, Vec(half, 0.0, 0.0)).has_value());
    REQUIRE(builder.addNode(4, Vec(-half / 2, 0.0, 0.0)).has_value());
    REQUIRE(builder.addNode(5, Vec(half / 2, 0.0, 0.0)).has_value());
    REQUIRE(builder
                .addElement(fem::core::ElementType::Line3, 1,
                            std::vector<fem::core::ExternalId>{1, 2, 4})
                .has_value());
    REQUIRE(builder
                .addElement(fem::core::ElementType::Line3, 2,
                            std::vector<fem::core::ExternalId>{2, 3, 5})
                .has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    model.assignments.push_back({.elements = fem::core::ElementIds{{1, 2}},
                                 .kind = fem::core::ElementKind::Beam2D,
                                 .material = "Steel",
                                 .section = "S"});
    model.foundations.push_back(
        {.elements = fem::core::ElementIds{{1, 2}}, .translational = Vec(0.0, k, 0.0)});
    fem::core::test::restrain(model, {2}, {fem::core::DofType::U});
    const double p = 1e5;
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0.0, -p, 0.0)});
    const fem::core::StaticResult result = fem::core::test::solve(model);
    CHECK(fem::core::test::near(result.displacements[1].y(), -p * beta / (2 * k), 1e-8));
}

namespace {

fem::core::IsoBeamData isoData()
{
    using namespace fem::core::test;
    fem::core::IsoBeamData data;
    data.beam.axial = kE * kArea;
    data.beam.torsional = kG * kJ;
    data.beam.bendingY = kE * kIy;
    data.beam.bendingZ = kE * kIz;
    data.beam.shearY = data.beam.shearZ = kG * kShearArea;
    data.beam.shearCentre = Eigen::Vector2d(0.01, -0.02);
    return data;
}

struct IsoComputed
{
    Eigen::MatrixXd k;
    Eigen::VectorXd f;
    bool ok = false;
};

IsoComputed isoCompute(const fem::core::IsoBeamData& data, const Vec& a, const Vec& b,
                       const Vec& middle)
{
    const std::vector<fem::core::IsoBeamData> beams = {data};
    const fem::core::IsoBeamElement element(fem::core::ElementKind::Beam3D, beams);
    fem::core::ElementNodes nodes;
    nodes.type = fem::core::ElementType::Line3;
    nodes.count = 3;
    nodes.x[0] = a;
    nodes.x[1] = b;
    nodes.x[2] = middle;
    IsoComputed out{.k = Eigen::MatrixXd::Zero(18, 18), .f = Eigen::VectorXd::Zero(18)};
    out.ok = element.compute(nodes, 0, out.k, out.f).has_value();
    return out;
}

} // namespace

// REQ-008b-14: the isoparametric element: rigid modes of a curved one,
// the patch test of a straight one, and a folded curve refused.
TEST_CASE("the isoparametric beam element", "[core][beam][curved][iso]")
{
    const fem::core::IsoBeamData data = isoData();

    SECTION("six rigid modes on a curve")
    {
        const Vec a(2.0, 0.0, 0.0);
        const Vec b(0.0, 2.0, 0.5);
        const Vec m(1.5, 1.3, 0.1);
        const IsoComputed e = isoCompute(data, a, b, m);
        REQUIRE(e.ok);
        CHECK((e.k - e.k.transpose()).norm() <= 1e-12 * e.k.norm());
        const std::array<Vec, 3> points = {a, b, m};
        for (int mode = 0; mode < 6; ++mode) {
            Vec t = Vec::Zero();
            Vec phi = Vec::Zero();
            (mode < 3 ? t : phi)(mode % 3) = 1.0;
            Eigen::VectorXd d(18);
            for (std::size_t i = 0; i < 3; ++i) {
                d.segment<3>(6 * static_cast<Eigen::Index>(i)) = t + phi.cross(points.at(i));
                d.segment<3>(6 * static_cast<Eigen::Index>(i) + 3) = phi;
            }
            CHECK((e.k * d).norm() <= 1e-9 * e.k.norm());
        }
    }
    SECTION("patch test: a straight cantilever under end moments, normal force and torque")
    {
        using namespace fem::core::test;
        const double length = 3.0;
        const IsoComputed e =
            isoCompute(data, Vec::Zero(), Vec(length, 0, 0), Vec(length / 2, 0, 0));
        REQUIRE(e.ok);
        // Free dofs: the end node (6 to 11) and the middle node (12 to 17).
        const Eigen::MatrixXd k = e.k.bottomRightCorner(12, 12);
        Eigen::VectorXd f = Eigen::VectorXd::Zero(12);
        const double n = 1e5;
        const double t = 2e3;
        const double my = 3e3;
        const double mz = -4e3;
        f.head<6>() << n, 0.0, 0.0, t, my, mz;
        const Eigen::VectorXd d = k.ldlt().solve(f);
        // Along X with the default reference, the local y is the global Z
        // and the local z is -Y: a moment about Z bends with EIy, about Y
        // with EIz; the twist moves the centroid by ey rx along Y.
        const double ey = data.beam.shearCentre.x();
        CHECK(std::abs(d(0) - n * length / (kE * kArea)) <= 1e-10 * std::abs(d(0)));
        CHECK(std::abs(d(3) - t * length / (kG * kJ)) <= 1e-10 * std::abs(d(3)));
        CHECK(std::abs(d(5) - mz * length / (kE * kIy)) <= 1e-10 * std::abs(d(5)));
        CHECK(std::abs(d(4) - my * length / (kE * kIz)) <= 1e-10 * std::abs(d(4)));
        CHECK(std::abs(d(1) - (mz * length * length / (2 * kE * kIy) + ey * d(3))) <=
              1e-10 * std::abs(d(1)));
    }
    SECTION("a curve that folds on itself is refused")
    {
        CHECK_FALSE(isoCompute(data, Vec::Zero(), Vec(1, 0, 0), Vec(0.9, 0, 0)).ok);
    }
}

namespace {

// Line3 isoparametric beams through the given end points, the middle nodes
// given too (ids: ends 1, 2, ...; middles 1001, ...). Steel, the generic
// section, Timoshenko.
fem::core::Model isoModel(const std::vector<Vec>& ends, const std::vector<Vec>& middles,
                          fem::core::ElementKind kind)
{
    fem::core::MeshBuilder builder;
    for (std::size_t i = 0; i < ends.size(); ++i) {
        REQUIRE(builder.addNode(static_cast<fem::core::ExternalId>(i + 1), ends[i]).has_value());
    }
    for (std::size_t i = 0; i < middles.size(); ++i) {
        REQUIRE(
            builder.addNode(static_cast<fem::core::ExternalId>(1001 + i), middles[i]).has_value());
    }
    fem::core::ElementIds all;
    for (std::size_t i = 0; i < middles.size(); ++i) {
        const std::vector<fem::core::ExternalId> line = {
            static_cast<fem::core::ExternalId>(i + 1), static_cast<fem::core::ExternalId>(i + 2),
            static_cast<fem::core::ExternalId>(1001 + i)};
        REQUIRE(builder
                    .addElement(fem::core::ElementType::Line3,
                                static_cast<fem::core::ExternalId>(i + 1), line)
                    .has_value());
        all.ids.push_back(static_cast<fem::core::ExternalId>(i + 1));
    }
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    model.assignments.push_back({.elements = all,
                                 .kind = kind,
                                 .material = "Steel",
                                 .section = "S",
                                 .theory = fem::core::BeamTheory::Timoshenko,
                                 .curved = fem::core::CurvedFormulation::Isoparametric});
    return model;
}

} // namespace

// REQ-008b-14, 19: the analysis with isoparametric beams: the patch test
// through the model, and the middle node with a support and a load.
TEST_CASE("analysis with isoparametric beams", "[core][beam][curved][iso]")
{
    using namespace fem::core::test;
    SECTION("patch test: two unequal straight elements under end moments")
    {
        fem::core::Model model =
            isoModel({Vec(0, 0, 0), Vec(1.0, 0, 0), Vec(3.0, 0, 0)},
                     {Vec(0.5, 0, 0), Vec(2.0, 0, 0)}, fem::core::ElementKind::Beam2D);
        fixAll(model, {1}, fem::core::ElementKind::Beam2D);
        const double m = 5e3;
        const double n = 2e5;
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{3}},
                                     .force = Vec(n, 0.0, 0.0),
                                     .moment = Vec(0.0, 0.0, m)});
        const fem::core::StaticResult result = solve(model);
        const double length = 3.0;
        CHECK(near(result.rotations[2].z(), m * length / (kE * kIz), 1e-10));
        CHECK(near(result.displacements[2].y(), m * length * length / (2 * kE * kIz), 1e-10));
        CHECK(near(result.displacements[2].x(), n * length / (kE * kArea), 1e-10));
        // The forces along the bars are constant.
        const fem::core::BeamResult& beam = beamOf(result, 1);
        CHECK(near(beam.at(0.7).forces.mz, m, 1e-10));
        CHECK(near(beam.at(1.3).forces.n, n, 1e-10));
    }
    SECTION("the middle node of a curved beam takes a support and a load")
    {
        const double r = 2.0;
        fem::core::Model model =
            isoModel({Vec(r, 0, 0), Vec(0, r, 0)}, {Vec(r * std::sqrt(0.5), r * std::sqrt(0.5), 0)},
                     fem::core::ElementKind::Beam3D);
        fixAll(model, {1}, fem::core::ElementKind::Beam3D);
        restrain(model, {1001}, {fem::core::DofType::W});
        const Vec force(1e3, -2e3, 5e2);
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{1001}}, .force = force});
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0, 0, 3e2)});
        const fem::core::StaticResult result = solve(model);
        // Equilibrium of the forces, with the supports at node 1 and the middle.
        const Vec total = result.reactions[0] + result.reactions[2] + force + Vec(0, 0, 3e2);
        CHECK(total.norm() <= 1e-9 * force.norm());
        CHECK(std::abs(result.displacements[2].z()) == 0.0);
        CHECK(result.displacements[2].norm() > 0.0);
    }
}
