// REQ-008b-12 to 16: curved beams (design 008b §5.2, 5.3).

#include "arc_reference.hpp"
#include "beam_models.hpp"

#include <fem/core/beam_element.hpp>
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
