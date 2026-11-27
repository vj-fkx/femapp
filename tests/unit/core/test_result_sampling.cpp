// Feature 012 (design §5): where the points of a bar are, and the samples
// of a result along the bars.

#include "bar_meshes.hpp"
#include "beam_models.hpp"

#include <fem/core/beam_result.hpp>
#include <fem/core/mesh_builder.hpp>
#include <fem/core/result_sampling.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

using fem::core::BeamPlacement;
using fem::core::BeamResult;
using fem::core::ElementKind;
using Vec = Eigen::Vector3d;

namespace {

constexpr double kPi = std::numbers::pi;

bool close(const Vec& value, const Vec& reference, double tolerance, double scale)
{
    return (value - reference).norm() <= tolerance * scale;
}

// A quarter of a circle of radius r in the XY plane, one Line3 from (r, 0)
// to (0, r), fixed at node 1 and loaded at node 2 (the middle node is 3).
fem::core::Model quarterRing(double r, fem::core::CurvedFormulation curved)
{
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec(r, 0.0, 0.0)).has_value());
    REQUIRE(builder.addNode(2, Vec(0.0, r, 0.0)).has_value());
    REQUIRE(builder.addNode(3, r * Vec(std::cos(kPi / 4), std::sin(kPi / 4), 0.0)).has_value());
    const std::vector<fem::core::ExternalId> line = {1, 2, 3};
    REQUIRE(builder.addElement(fem::core::ElementType::Line3, 1, line).has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    fem::core::Model model;
    model.mesh = std::make_shared<const fem::core::Mesh>(std::move(*mesh));
    model.materials.push_back(fem::core::test::steel());
    model.sections.push_back(fem::core::test::genericSection());
    model.assignments.push_back({.elements = fem::core::ElementIds{{1}},
                                 .kind = ElementKind::Beam3D,
                                 .material = "Steel",
                                 .section = "S",
                                 .theory = fem::core::BeamTheory::Timoshenko,
                                 .curved = curved});
    fem::core::test::fixAll(model, {1}, ElementKind::Beam3D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(2e3, -3e3, 1.5e3)});
    return model;
}

// The global displacement of the centroid axis at x.
Vec globalDisplacement(const BeamResult& beam, double x)
{
    return beam.placementAt(x).axes * beam.at(x).displacement;
}

} // namespace

// REQ-012-05 (design §5.1): the ends of a straight bar are its nodes moved
// by the rigid offsets, and its points lie on the line between them.
TEST_CASE("the points of a straight bar", "[core][result][sampling]")
{
    using namespace fem::core::test;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(4, 1, 0.5)},
                         .bars = {{0, 1}},
                         .lineGroups = {},
                         .pointGroups = {}};
    const Vec start(0.1, -0.2, 0.05);
    const Vec end(-0.15, 0.1, 0.0);
    for (const bool offset : {false, true}) {
        CAPTURE(offset);
        fem::core::Model model = beamModel(spec, ElementKind::Beam3D);
        if (offset) {
            model.rigidOffsets.push_back(
                {.elements = fem::core::ElementIds{{1}}, .start = start, .end = end});
        }
        fixAll(model, {1}, ElementKind::Beam3D);
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}},
                                     .force = Vec(1e3, -2e3, 3e3),
                                     .moment = Vec(100, 200, -300)});
        const fem::core::StaticResult result = solve(model);
        const BeamResult& beam = beamOf(result, 0);
        const Vec a = spec.nodes[0] + (offset ? start : Vec::Zero());
        const Vec b = spec.nodes[1] + (offset ? end : Vec::Zero());
        CHECK(close(beam.origin(), a, 1e-12, 1.0));
        CHECK(close(beam.placementAt(0.0).position, a, 1e-12, 1.0));
        CHECK(close(beam.placementAt(beam.length()).position, b, 1e-12, 1.0));
        const BeamPlacement middle = beam.placementAt(beam.length() / 2);
        CHECK(close(middle.position, (a + b) / 2, 1e-12, 1.0));
        CHECK((middle.axes - beam.axes()).norm() <= 1e-15);
        // The flexible ends move with their nodes, the offset as a rigid
        // link: u + theta x offset.
        const double scale = result.displacements[1].norm();
        CHECK(close(globalDisplacement(beam, 0.0), Vec::Zero(), 1e-9, scale));
        const Vec tip =
            result.displacements[1] + result.rotations[1].cross(offset ? end : Vec::Zero());
        CHECK(close(globalDisplacement(beam, beam.length()), tip, 1e-9, scale));
    }
}

// REQ-012-05 (design §5.1): the points of an arc, and of the isoparametric
// beam through the same nodes, lie on the curve; at the nodes, the global
// displacement is the nodal one.
TEST_CASE("the points of a curved bar", "[core][result][sampling]")
{
    const double r = 2.0;
    for (const auto curved :
         {fem::core::CurvedFormulation::Arc, fem::core::CurvedFormulation::Isoparametric}) {
        const bool arc = curved == fem::core::CurvedFormulation::Arc;
        CAPTURE(arc);
        const fem::core::StaticResult result = fem::core::test::solve(quarterRing(r, curved));
        const BeamResult& beam = fem::core::test::beamOf(result, 0);
        const double length = beam.length();
        CHECK(close(beam.placementAt(0.0).position, Vec(r, 0, 0), 1e-12, r));
        CHECK(close(beam.placementAt(length).position, Vec(0, r, 0), 1e-12, r));
        const Vec middle = r * Vec(std::cos(kPi / 4), std::sin(kPi / 4), 0.0);
        // Isoparametric: the arc length of the parabola is integrated
        // numerically, so its half is the middle node only to that accuracy.
        const double atMiddle = arc ? 1e-12 : 1e-6;
        CHECK(close(beam.placementAt(length / 2).position, middle, atMiddle, r));
        for (const double s : {0.0, 0.3 * length, length / 2, length}) {
            CAPTURE(s);
            const BeamPlacement p = beam.placementAt(s);
            // An arc is the circle; the Line3 is a parabola through the
            // nodes, close to it.
            CHECK(std::abs(p.position.norm() - r) <= (arc ? 1e-12 : 1e-2) * r);
            CHECK((p.axes.transpose() * p.axes - Eigen::Matrix3d::Identity()).norm() <= 1e-13);
            if (arc) {
                const Vec tangent = Vec(-std::sin(s / r), std::cos(s / r), 0.0);
                CHECK(close(p.axes.col(0), tangent, 1e-12, 1.0));
            }
        }
        const double scale = result.displacements[1].norm();
        CHECK(close(globalDisplacement(beam, 0.0), Vec::Zero(), 1e-9, scale));
        CHECK(close(globalDisplacement(beam, length), result.displacements[1], 1e-9, scale));
        if (!arc) {
            // The middle node, at half the arc length of the symmetric curve.
            CHECK(close(globalDisplacement(beam, length / 2), result.displacements[2], atMiddle,
                        scale));
        }
    }
}

// REQ-012-05: the bars of a buckling mode know where they are too.
TEST_CASE("the points of the bars of a buckling mode", "[core][result][sampling]")
{
    using namespace fem::core::test;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(0, 3, 0), Vec(0, 6, 0)},
                         .bars = {{0, 1}, {1, 2}},
                         .lineGroups = {},
                         .pointGroups = {}};
    fem::core::Model model = beamModel(spec, ElementKind::Beam2D);
    fixAll(model, {1}, ElementKind::Beam2D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{3}}, .force = Vec(0, -1e3, 0)});
    auto buckling = fem::core::runLinearBuckling(model);
    REQUIRE(buckling.has_value());
    REQUIRE_FALSE(buckling->modes.empty());
    const fem::core::BucklingMode& mode = buckling->modes.front();
    for (std::size_t e = 0; e < 2; ++e) {
        const auto& beam = mode.beams.at(e);
        if (beam) {
            CHECK(close(beam->placementAt(0.0).position, spec.nodes[e], 1e-12, 1.0));
            CHECK(close(beam->placementAt(beam->length()).position, spec.nodes[e + 1], 1e-12, 1.0));
        }
    }
}

namespace {

constexpr double kSpan = 6.0;
constexpr double kQ = 1.0e4; // N/m, downward

// A simply supported 2D beam of span kSpan along x under a uniform load kQ
// (and, when given, a concentrated force at the fraction `at`).
fem::core::Model simpleBeam(std::optional<double> at = std::nullopt)
{
    using namespace fem::core::test;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(kSpan, 0, 0)},
                         .bars = {{0, 1}},
                         .lineGroups = {},
                         .pointGroups = {}};
    fem::core::Model model = beamModel(spec, ElementKind::Beam2D);
    restrain(model, {1}, {fem::core::DofType::U, fem::core::DofType::V});
    restrain(model, {2}, {fem::core::DofType::V});
    model.distributedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                      .startValue = Vec(0, -kQ, 0),
                                      .endValue = Vec(0, -kQ, 0)});
    if (at) {
        model.concentratedLoads.push_back({.elements = fem::core::ElementIds{{1}},
                                           .force = Vec(0, -2e4, 0),
                                           .at = {.value = *at, .fraction = true}});
    }
    return model;
}

std::vector<double> xs(const fem::core::ShapeSamples& shape)
{
    std::vector<double> out;
    out.reserve(shape.stations.size());
    for (const fem::core::Station& s : shape.stations) {
        out.push_back(s.x);
    }
    return out;
}

} // namespace

// REQ-012-05, AC-1: the deformed shape of a simply supported beam: the
// samples are the points of the core, and the middle has the deflection
// 5 q L^4 / 384 EI.
TEST_CASE("the deformed shape of a simply supported beam", "[core][result][sampling]")
{
    const fem::core::StaticResult result = fem::core::test::solve(simpleBeam());
    const fem::core::ShapeSamples shape =
        fem::core::sampleShape(fem::core::fieldsOf(result), {.perBeam = 9, .threads = 0});
    REQUIRE(shape.bars.size() == 1);
    CHECK(shape.bars[0].element == 0);
    CHECK(shape.bars[0].first == 0);
    REQUIRE(shape.bars[0].count == 9);
    REQUIRE(shape.positions.size() == 9);
    const BeamResult& beam = fem::core::test::beamOf(result, 0);
    for (std::size_t k = 0; k < 9; ++k) {
        const fem::core::Station& s = shape.stations[k];
        CAPTURE(k, s.x);
        CHECK(s.node == -1);
        CHECK(close(shape.positions[k], beam.placementAt(s.x).position, 1e-12, kSpan));
        CHECK(close(shape.displacements[k], globalDisplacement(beam, s.x), 1e-12,
                    shape.displacements[4].norm()));
    }
    CHECK(shape.stations.front().x == 0.0);
    CHECK(shape.stations[4].x == kSpan / 2);
    CHECK(shape.stations.back().x == kSpan);
    const double ei = fem::core::test::kE * fem::core::test::kIz;
    const double deflection = 5 * kQ * std::pow(kSpan, 4) / (384 * ei);
    CHECK(std::abs(shape.displacements[4].y() + deflection) <= 1e-9 * deflection);
    CHECK(std::abs(shape.displacements[4].x()) <= 1e-12 * deflection);
    CHECK(close(shape.bounds.min, Vec::Zero(), 1e-15, 1.0));
    CHECK(close(shape.bounds.max, Vec(kSpan, 0, 0), 1e-15, 1.0));
}

// REQ-012-05, 10: the stations of the bars.
TEST_CASE("the stations of the bars", "[core][result][sampling]")
{
    using namespace fem::core::test;
    SECTION("both sides of a concentrated load, merged with a uniform point")
    {
        for (const double at : {0.3, 0.5}) {
            CAPTURE(at);
            const fem::core::StaticResult result = solve(simpleBeam(at));
            const fem::core::ShapeSamples shape =
                fem::core::sampleShape(fem::core::fieldsOf(result), {.perBeam = 9, .threads = 0});
            const double x = at * kSpan;
            // 9 uniform points; in the middle, the After side is one of them.
            CHECK(shape.stations.size() == (at == 0.5 ? 10U : 11U));
            const auto here = std::ranges::find_if(
                shape.stations, [&](const fem::core::Station& s) { return s.x == x; });
            REQUIRE(here != shape.stations.end());
            CHECK(here->side == fem::core::Side::Before);
            REQUIRE(std::next(here) != shape.stations.end());
            CHECK(std::next(here)->x == x);
            CHECK(std::next(here)->side == fem::core::Side::After);
            CHECK(std::ranges::is_sorted(xs(shape)));
            const auto i = static_cast<std::size_t>(here - shape.stations.begin());
            CHECK(close(shape.positions[i], shape.positions[i + 1], 1e-15, 1.0));
        }
    }
    SECTION("the junctions of the parts and the nodes of rigid links")
    {
        BeamResult::Data data;
        data.length = 4.0;
        data.junctions = {1.0, 3.5};
        const BeamResult beam(std::move(data));
        const std::vector<fem::core::Station> stations = fem::core::beamStations(beam, 5, 3, 7);
        REQUIRE(stations.size() == 8);
        CHECK(stations.front().node == 3);
        CHECK(stations.front().x == 0.0);
        CHECK(stations.back().node == 7);
        CHECK(stations.back().x == 4.0);
        std::vector<double> inside;
        for (std::size_t k = 1; k + 1 < stations.size(); ++k) {
            CHECK(stations[k].node == -1);
            inside.push_back(stations[k].x);
        }
        CHECK(inside == std::vector<double>{0.0, 1.0, 2.0, 3.0, 3.5, 4.0});
    }
    SECTION("a rigid link: the node, then the flexible length")
    {
        const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(4, 0, 0)},
                             .bars = {{0, 1}},
                             .lineGroups = {},
                             .pointGroups = {}};
        fem::core::Model model = beamModel(spec, ElementKind::Beam3D);
        const Vec start(0.2, 0.1, 0.0);
        model.rigidOffsets.push_back({.elements = fem::core::ElementIds{{1}}, .start = start});
        fixAll(model, {1}, ElementKind::Beam3D);
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(0, 0, -1e3)});
        const fem::core::StaticResult result = solve(model);
        const fem::core::ShapeSamples shape =
            fem::core::sampleShape(fem::core::fieldsOf(result), {.perBeam = 5, .threads = 0});
        REQUIRE(shape.stations.size() == 6); // the start node, then 5 points
        CHECK(shape.stations[0].node == 0);
        CHECK(close(shape.positions[0], spec.nodes[0], 1e-15, 1.0));
        CHECK(close(shape.displacements[0], result.displacements[0], 1e-15, 1.0));
        CHECK(shape.stations[1].node == -1);
        CHECK(close(shape.positions[1], start, 1e-12, 1.0));
        CHECK(shape.stations.back().node == -1); // no link at the end
    }
    SECTION("a truss: its two nodes")
    {
        const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(3, 4, 0), Vec(6, 0, 0)},
                             .bars = {{0, 1}, {1, 2}, {0, 2}},
                             .lineGroups = {},
                             .pointGroups = {}};
        fem::core::Model model = beamModel(spec, ElementKind::Truss2D);
        restrain(model, {1}, {fem::core::DofType::U, fem::core::DofType::V});
        restrain(model, {3}, {fem::core::DofType::V});
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(1e3, -2e3, 0)});
        const fem::core::StaticResult result = solve(model);
        const fem::core::ShapeSamples shape = fem::core::sampleShape(fem::core::fieldsOf(result));
        REQUIRE(shape.bars.size() == 3);
        REQUIRE(shape.stations.size() == 6);
        CHECK(shape.bars[0].count == 2);
        CHECK(shape.stations[0].node == 0);
        CHECK(shape.stations[1].node == 1);
        CHECK(shape.stations[1].x == 5.0);
        CHECK(close(shape.positions[1], spec.nodes[1], 1e-15, 1.0));
        CHECK(close(shape.displacements[1], result.displacements[1], 1e-15, 1.0));
    }
    SECTION("the samples of a beam from the budget")
    {
        CHECK(fem::core::samplesPerBeam(0) == 17);
        CHECK(fem::core::samplesPerBeam(10) == 17);
        CHECK(fem::core::samplesPerBeam(58'000) == 17);
        CHECK(fem::core::samplesPerBeam(100'000) == 9);
        CHECK(fem::core::samplesPerBeam(120'000) == 7);
        CHECK(fem::core::samplesPerBeam(200'000) == 5);
        CHECK(fem::core::samplesPerBeam(10'000'000) == 5);
        const fem::core::StaticResult result = solve(simpleBeam());
        const fem::core::ShapeSamples shape = fem::core::sampleShape(fem::core::fieldsOf(result));
        CHECK(shape.stations.size() == 17);
    }
}

// REQ-012-09, 17, AC-2: Mz along the simply supported beam: qL^2/8 in the
// middle, the largest value.
TEST_CASE("the bending moment of a simply supported beam", "[core][result][sampling]")
{
    using fem::core::ResultQuantity;
    const fem::core::StaticResult result = fem::core::test::solve(simpleBeam());
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 9});
    const fem::core::FieldSamples mz = fem::core::sampleField(fields, shape, ResultQuantity::Mz);
    CHECK(mz.quantity == ResultQuantity::Mz);
    REQUIRE(mz.values.size() == 9);
    const double expected = kQ * kSpan * kSpan / 8;
    CHECK(mz.max.sample == 4);
    CHECK(std::abs(mz.max.value - expected) <= 1e-9 * expected);
    CHECK(std::abs(mz.min.value) <= 1e-9 * expected);
    // The deflection: the smallest Uy, in the middle.
    const fem::core::FieldSamples uy = fem::core::sampleField(fields, shape, ResultQuantity::Uy);
    CHECK(uy.min.sample == 4);
    CHECK(uy.min.value == shape.displacements[4].y());
    // Nothing out of the plane: every value 0, the extremes at the first.
    const fem::core::FieldSamples vz = fem::core::sampleField(fields, shape, ResultQuantity::Vz);
    CHECK(vz.max.sample == 0);
    CHECK(vz.min.sample == 0);
    CHECK(vz.max.value == 0.0);
}

// REQ-012-10, AC-2: each side of a concentrated load has its own forces.
TEST_CASE("the forces on both sides of a concentrated load", "[core][result][sampling]")
{
    const fem::core::StaticResult result = fem::core::test::solve(simpleBeam(0.3));
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 9});
    const fem::core::FieldSamples vy =
        fem::core::sampleField(fields, shape, fem::core::ResultQuantity::Vy);
    const double x = 0.3 * kSpan;
    const auto here =
        std::ranges::find_if(shape.stations, [&](const fem::core::Station& s) { return s.x == x; });
    REQUIRE(here != shape.stations.end());
    const auto i = static_cast<std::size_t>(here - shape.stations.begin());
    const BeamResult& beam = fem::core::test::beamOf(result, 0);
    CHECK(vy.values[i] == beam.at(x, fem::core::Side::Before).forces.vy);
    CHECK(vy.values[i + 1] == beam.at(x, fem::core::Side::After).forces.vy);
    CHECK(std::abs(std::abs(vy.values[i + 1] - vy.values[i]) - 2e4) <= 1e-9 * 2e4);
}

// REQ-012-10, AC-2: at a node of a frame, each bar keeps its own forces.
TEST_CASE("the forces of a frame are not averaged at its nodes", "[core][result][sampling]")
{
    using namespace fem::core::test;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(0, 3, 0), Vec(4, 3, 0)},
                         .bars = {{0, 1}, {1, 2}},
                         .lineGroups = {},
                         .pointGroups = {}};
    fem::core::Model model = beamModel(spec, ElementKind::Beam2D);
    fixAll(model, {1, 3}, ElementKind::Beam2D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(5e3, -1e4, 0)});
    const fem::core::StaticResult result = solve(model);
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 5});
    REQUIRE(shape.bars.size() == 2);
    for (const auto quantity : {fem::core::ResultQuantity::N, fem::core::ResultQuantity::Mz}) {
        const fem::core::FieldSamples field = fem::core::sampleField(fields, shape, quantity);
        const std::size_t columnTop = shape.bars[0].first + shape.bars[0].count - 1;
        const std::size_t beamStart = shape.bars[1].first;
        const BeamResult& column = beamOf(result, 0);
        const BeamResult& beam = beamOf(result, 1);
        const bool axial = quantity == fem::core::ResultQuantity::N;
        CHECK(field.values[columnTop] ==
              (axial ? column.at(column.length()).forces.n : column.at(column.length()).forces.mz));
        CHECK(field.values[beamStart] == (axial ? beam.at(0.0).forces.n : beam.at(0.0).forces.mz));
    }
    const fem::core::FieldSamples n =
        fem::core::sampleField(fields, shape, fem::core::ResultQuantity::N);
    CHECK(n.values[shape.bars[0].first + shape.bars[0].count - 1] != n.values[shape.bars[1].first]);
}

// REQ-012-09: displacements and rotations in global axes, whatever the
// direction of the bar.
TEST_CASE("the global quantities of a turned bar", "[core][result][sampling]")
{
    using namespace fem::core::test;
    using fem::core::ResultQuantity;
    const double angle = kPi / 6;
    const TrussSpec spec{.nodes = {Vec(0, 0, 0), 3.0 * Vec(std::cos(angle), std::sin(angle), 0)},
                         .bars = {{0, 1}},
                         .lineGroups = {},
                         .pointGroups = {}};
    fem::core::Model model = beamModel(spec, ElementKind::Beam3D);
    fixAll(model, {1}, ElementKind::Beam3D);
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}},
                                 .force = Vec(1e3, -2e3, -3e3),
                                 .moment = Vec(100, 0, 50)});
    const fem::core::StaticResult result = solve(model);
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 5});
    const std::size_t tip = shape.stations.size() - 1;
    const Vec u = result.displacements[1];
    const Vec r = result.rotations[1];
    const auto value = [&](ResultQuantity q) {
        return fem::core::sampleField(fields, shape, q).values[tip];
    };
    CHECK(std::abs(value(ResultQuantity::Ux) - u.x()) <= 1e-9 * u.norm());
    CHECK(std::abs(value(ResultQuantity::Uy) - u.y()) <= 1e-9 * u.norm());
    CHECK(std::abs(value(ResultQuantity::Uz) - u.z()) <= 1e-9 * u.norm());
    CHECK(std::abs(value(ResultQuantity::UMagnitude) - u.norm()) <= 1e-9 * u.norm());
    CHECK(std::abs(value(ResultQuantity::Rx) - r.x()) <= 1e-9 * r.norm());
    CHECK(std::abs(value(ResultQuantity::Ry) - r.y()) <= 1e-9 * r.norm());
    CHECK(std::abs(value(ResultQuantity::Rz) - r.z()) <= 1e-9 * r.norm());
}

// REQ-012-09: a truss has only N; a buckling mode has no forces.
TEST_CASE("the quantities of trusses and of buckling modes", "[core][result][sampling]")
{
    using namespace fem::core::test;
    using fem::core::ResultQuantity;
    SECTION("a truss")
    {
        const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(3, 4, 0), Vec(6, 0, 0)},
                             .bars = {{0, 1}, {1, 2}, {0, 2}},
                             .lineGroups = {},
                             .pointGroups = {}};
        fem::core::Model model = beamModel(spec, ElementKind::Truss2D);
        restrain(model, {1}, {fem::core::DofType::U, fem::core::DofType::V});
        restrain(model, {3}, {fem::core::DofType::V});
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(1e3, -2e3, 0)});
        const fem::core::StaticResult result = solve(model);
        const fem::core::ResultFields fields = fem::core::fieldsOf(result);
        const fem::core::ShapeSamples shape = fem::core::sampleShape(fields);
        const fem::core::FieldSamples n = fem::core::sampleField(fields, shape, ResultQuantity::N);
        const fem::core::FieldSamples vy =
            fem::core::sampleField(fields, shape, ResultQuantity::Vy);
        for (std::size_t b = 0; b < 3; ++b) {
            for (std::uint32_t k = 0; k < 2; ++k) {
                CHECK(n.values[shape.bars[b].first + k] == result.axialForce[b]);
                CHECK(vy.values[shape.bars[b].first + k] == 0.0);
            }
        }
        // In the middle of a truss: linear between its nodes.
        const Vec middle = fem::core::displacementAt(
            fields, 0, fem::core::Station{.x = 2.5, .side = fem::core::Side::After, .node = -1});
        CHECK(close(middle, (result.displacements[0] + result.displacements[1]) / 2, 1e-15,
                    result.displacements[1].norm()));
    }
    SECTION("a buckling mode")
    {
        const TrussSpec spec{.nodes = {Vec(0, 0, 0), Vec(0, 3, 0), Vec(0, 6, 0)},
                             .bars = {{0, 1}, {1, 2}},
                             .lineGroups = {},
                             .pointGroups = {}};
        fem::core::Model model = beamModel(spec, ElementKind::Beam2D);
        fixAll(model, {1}, ElementKind::Beam2D);
        model.nodalForces.push_back({.nodes = fem::core::NodeIds{{3}}, .force = Vec(0, -1e3, 0)});
        const fem::core::StaticResult base = solve(model);
        auto buckling = fem::core::runLinearBuckling(model);
        REQUIRE(buckling.has_value());
        REQUIRE_FALSE(buckling->modes.empty());
        const fem::core::BucklingMode& mode = buckling->modes.front();
        const fem::core::ResultFields fields = fem::core::fieldsOf(base.mesh, mode);
        CHECK(fields.mode);
        const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 5});
        const fem::core::FieldSamples mz =
            fem::core::sampleField(fields, shape, ResultQuantity::Mz);
        CHECK(std::ranges::all_of(mz.values, [](double v) { return std::isnan(v); }));
        CHECK(mz.max.value == 0.0);
        CHECK(mz.max.sample == 0);
        // The top moves sideways in the first mode.
        const fem::core::FieldSamples ux =
            fem::core::sampleField(fields, shape, ResultQuantity::Ux);
        const std::size_t top = shape.stations.size() - 1;
        CHECK(std::abs(ux.values[top] - mode.displacements[2].x()) <=
              1e-9 * mode.displacements[2].norm());
        CHECK(std::abs(mode.displacements[2].x()) > 0.0);
    }
}

// REQ-012-18, AC-6: a point between the samples gives the value of the core
// there, not that of a sample next to it.
TEST_CASE("the value at a point of a bar", "[core][result][sampling]")
{
    using fem::core::ResultQuantity;
    const fem::core::StaticResult result = fem::core::test::solve(simpleBeam());
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples shape = fem::core::sampleShape(fields, {.perBeam = 9});
    const fem::core::FieldSamples mz = fem::core::sampleField(fields, shape, ResultQuantity::Mz);
    const BeamResult& beam = fem::core::test::beamOf(result, 0);
    const double x = 1.1; // between the samples at 0.75 and 1.5
    const double value = fem::core::valueAt(
        fields, 0, fem::core::Station{.x = x, .side = fem::core::Side::After, .node = -1},
        ResultQuantity::Mz);
    CHECK(value == beam.at(x).forces.mz);
    CHECK(std::abs(value - kQ * x * (kSpan - x) / 2) <= 1e-9 * kQ * kSpan * kSpan);
    CHECK(value != mz.values[1]);
    CHECK(value != mz.values[2]);
    CHECK(close(
        fem::core::displacementAt(
            fields, 0, fem::core::Station{.x = x, .side = fem::core::Side::After, .node = -1}),
        globalDisplacement(beam, x), 1e-15, 1.0));
    // An element that is not a bar has no value.
    CHECK(std::isnan(fem::core::valueAt(
        fields, 5, fem::core::Station{.x = 0.0, .side = fem::core::Side::After, .node = -1},
        ResultQuantity::Ux)));
}

// REQ-012-06, AC-1: the automatic factor makes the largest displacement 10%
// of the largest side of the bars.
TEST_CASE("the automatic scale factor", "[core][result][sampling]")
{
    const fem::core::StaticResult result = fem::core::test::solve(simpleBeam());
    const fem::core::ShapeSamples shape =
        fem::core::sampleShape(fem::core::fieldsOf(result), {.perBeam = 9, .threads = 0});
    const double largest = shape.displacements[4].norm(); // the middle
    const double factor = fem::core::autoScale(shape);
    CHECK(std::abs(factor - 0.1 * kSpan / largest) <= 1e-12 * factor);
    CHECK(std::abs(factor * largest - 0.1 * kSpan) <= 1e-12 * kSpan);

    fem::core::ShapeSamples still;
    still.positions = {Vec(0, 0, 0), Vec(2, 0, 0)};
    still.displacements = {Vec::Zero(), Vec::Zero()};
    still.bounds = {.min = Vec(0, 0, 0), .max = Vec(2, 0, 0), .empty = false};
    CHECK(fem::core::autoScale(still) == 1.0);
    still.displacements[1] = Vec(0, 1e-3, 0);
    CHECK(std::abs(fem::core::autoScale(still) - 200.0) <= 1e-12 * 200.0);
    CHECK(fem::core::autoScale(fem::core::ShapeSamples{}) == 1.0);
}

// NFR-012-02: the samples are the same with one thread and with all.
TEST_CASE("the samples do not depend on the threads", "[core][result][sampling]")
{
    using namespace fem::core::test;
    // A frame of 40 bays: columns and beams, a load on every beam.
    TrussSpec spec{.nodes = {}, .bars = {}, .lineGroups = {}, .pointGroups = {}};
    constexpr int kBays = 40;
    for (int i = 0; i <= kBays; ++i) {
        spec.nodes.emplace_back(3.0 * i, 0.0, 0.0);
        spec.nodes.emplace_back(3.0 * i, 4.0, 0.0);
    }
    for (int i = 0; i <= kBays; ++i) {
        spec.bars.push_back({2 * i, 2 * i + 1});
        if (i < kBays) {
            spec.bars.push_back({2 * i + 1, 2 * i + 3});
        }
    }
    fem::core::Model model = beamModel(spec, ElementKind::Beam2D);
    for (int i = 0; i <= kBays; ++i) {
        fixAll(model, {static_cast<fem::core::ExternalId>(2 * i + 1)}, ElementKind::Beam2D);
    }
    fem::core::ElementIds beams;
    for (std::size_t j = 0; j < spec.bars.size(); ++j) {
        if (j % 2 == 1) {
            beams.ids.push_back(static_cast<fem::core::ExternalId>(j) + 1);
        }
    }
    model.distributedLoads.push_back(
        {.elements = beams, .startValue = Vec(0, -kQ, 0), .endValue = Vec(0, -kQ, 0)});
    model.concentratedLoads.push_back(
        {.elements = beams, .force = Vec(1e3, -2e3, 0), .at = {.value = 0.3, .fraction = true}});
    model.nodalForces.push_back({.nodes = fem::core::NodeIds{{2}}, .force = Vec(5e3, 0, 0)});
    const fem::core::StaticResult result = solve(model);
    const fem::core::ResultFields fields = fem::core::fieldsOf(result);
    const fem::core::ShapeSamples one =
        fem::core::sampleShape(fields, {.perBeam = 0, .threads = 1});
    const fem::core::ShapeSamples all =
        fem::core::sampleShape(fields, {.perBeam = 0, .threads = 0});
    REQUIRE(one.positions.size() == all.positions.size());
    CHECK(one.positions == all.positions);
    CHECK(one.displacements == all.displacements);
    REQUIRE(one.bars.size() == all.bars.size());
    for (std::size_t b = 0; b < one.bars.size(); ++b) {
        CHECK(one.bars[b].first == all.bars[b].first);
        CHECK(one.bars[b].count == all.bars[b].count);
    }
    for (const auto quantity :
         {fem::core::ResultQuantity::UMagnitude, fem::core::ResultQuantity::Mz}) {
        const fem::core::FieldSamples a = fem::core::sampleField(fields, one, quantity, 1);
        const fem::core::FieldSamples b = fem::core::sampleField(fields, all, quantity, 0);
        CHECK(a.values == b.values);
        CHECK(a.max.sample == b.max.sample);
        CHECK(a.min.sample == b.min.sample);
    }
}
