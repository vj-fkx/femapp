#include <fem/mesher/line_mesher.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace {

using fem::core::ElementType;
using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::EntityId;
using fem::geom::LineEntity;
using fem::geom::PointEntity;
using fem::geom::PolylineEntity;
using fem::geom::Vec3;
using fem::mesher::LineMeshError;
using fem::mesher::meshLines;

EntityId addLine(Drawing& drawing, const Vec3& a, const Vec3& b)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = LineEntity{.start = a, .end = b}});
    drawing.apply(edit);
    return id;
}

EntityId addPolyline(Drawing& drawing, std::vector<Vec3> vertices, bool closed)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{
        .id = id, .shape = PolylineEntity{.vertices = std::move(vertices), .closed = closed}});
    drawing.apply(edit);
    return id;
}

EntityId addPoint(Drawing& drawing, const Vec3& p)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = PointEntity{.position = p}});
    drawing.apply(edit);
    return id;
}

// The node with external id `id`, by its coordinates.
Vec3 nodeAt(const fem::core::Mesh& mesh, fem::core::ExternalId id)
{
    const auto node = mesh.findNode(id);
    REQUIRE(node.has_value());
    return mesh.nodeCoordinates(*node);
}

// The external ids of the nodes of the element with external id `id`.
std::vector<fem::core::ExternalId> elementNodeIds(const fem::core::Mesh& mesh,
                                                  fem::core::ExternalId id)
{
    const auto element = mesh.findElement(id);
    REQUIRE(element.has_value());
    std::vector<fem::core::ExternalId> ids;
    for (const fem::core::NodeIndex n : mesh.elementNodes(*element)) {
        ids.push_back(mesh.nodeId(n));
    }
    return ids;
}

// A plane frame of two bays (4 wide) and two storeys (3 high), each column
// and each beam one line, touching at the ends: columns first, then the
// beams. The upper end of the first column is off by less than the
// tolerance from the start of the column above it.
struct Frame
{
    Drawing drawing;
    std::vector<EntityId> columns;
    std::vector<EntityId> beams;
};

void drawFrame(Frame& f)
{
    for (int c = 0; c < 3; ++c) {
        const double x = 4.0 * c;
        for (int s = 0; s < 2; ++s) {
            const Vec3 bottom(x, 3.0 * s, 0.0);
            Vec3 top(x, 3.0 * (s + 1), 0.0);
            if (c == 0 && s == 0) {
                top.x() += 1e-9; // within the tolerance (about 1e-8 here)
            }
            f.columns.push_back(addLine(f.drawing, bottom, top));
        }
    }
    for (int s = 1; s <= 2; ++s) {
        for (int b = 0; b < 2; ++b) {
            f.beams.push_back(
                addLine(f.drawing, Vec3(4.0 * b, 3.0 * s, 0.0), Vec3(4.0 * (b + 1), 3.0 * s, 0.0)));
        }
    }
}

} // namespace

// REQ-009-09 (REQ-001-10: the module is linked and callable).
TEST_CASE("the group of an entity is named by its id", "[mesher][line]")
{
    CHECK(fem::mesher::entityGroupName(EntityId{42}) == "entity 42");
    CHECK(fem::mesher::entityGroupName(EntityId{1}) == "entity 1");
}

// T05 (AC-1, AC-5): REQ-009-01, 02, 05, 08, 09, 10, 11.
TEST_CASE("a frame of lines touching at their ends", "[mesher][line]")
{
    Frame f;
    drawFrame(f);
    const auto result = meshLines(f.drawing);
    REQUIRE(result.has_value());
    const fem::core::Mesh& mesh = result->mesh;

    // 3 x 3 nodes, one element per line; 20 ends, 11 of them merged.
    CHECK(mesh.nodeCount() == 9);
    CHECK(mesh.elementCount() == 10);
    CHECK(mesh.elementCount(ElementType::Line2) == 10);
    CHECK(result->report.nodes == 9);
    CHECK(result->report.elements == 10);
    CHECK(result->report.mergedNodes == 11);
    CHECK(result->report.splits.empty());
    CHECK(result->report.ignored.empty());

    // REQ-009-08: ids along the entities in drawing order; a merged node
    // keeps the first position drawn.
    CHECK(nodeAt(mesh, 1) == Vec3(0, 0, 0));
    CHECK(nodeAt(mesh, 2) == Vec3(1e-9, 3, 0));
    CHECK(nodeAt(mesh, 3) == Vec3(0, 6, 0));
    CHECK(nodeAt(mesh, 4) == Vec3(4, 0, 0));
    CHECK(nodeAt(mesh, 9) == Vec3(8, 6, 0));
    CHECK(elementNodeIds(mesh, 1) == std::vector<fem::core::ExternalId>{1, 2});
    CHECK(elementNodeIds(mesh, 2) == std::vector<fem::core::ExternalId>{2, 3});
    // The first beam, from (0, 3) to (4, 3).
    CHECK(elementNodeIds(mesh, 7) == std::vector<fem::core::ExternalId>{2, 5});

    // REQ-009-09: one group of dimension 1 per line.
    for (const EntityId id : f.columns) {
        const auto* group = mesh.findGroup(fem::mesher::entityGroupName(id), 1);
        REQUIRE(group != nullptr);
        CHECK(group->elements.size() == 1);
    }
    CHECK(mesh.groups().size() == 10);

    // REQ-009-10: the map.
    REQUIRE(result->map.elements.size() == 10);
    REQUIRE(result->map.nodes.size() == 9);
    const auto first = mesh.findElement(1);
    REQUIRE(first.has_value());
    CHECK(result->map.elements[static_cast<std::size_t>(*first)].entity == f.columns[0]);
    CHECK(result->map.elements[static_cast<std::size_t>(*first)].segment == 0);
    const auto corner = mesh.findNode(2);
    REQUIRE(corner.has_value());
    const auto& origin = result->map.nodes[static_cast<std::size_t>(*corner)];
    // Through (0, 3): both columns of the first line and the first beam.
    CHECK(origin.entities == std::vector<EntityId>{f.columns[0], f.columns[1], f.beams[0]});
    const auto vertex = origin.vertex.value_or(std::pair{EntityId{}, -1});
    CHECK(vertex.first == f.columns[0]);
    CHECK(vertex.second == 1);
}

TEST_CASE("polylines, open and closed, and points", "[mesher][line]")
{
    Drawing drawing;
    const EntityId open =
        addPolyline(drawing, {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0)}, false);
    const EntityId closed =
        addPolyline(drawing, {Vec3(3, 0, 0), Vec3(4, 0, 0), Vec3(4, 1, 0), Vec3(3, 1, 0)}, true);
    const EntityId point = addPoint(drawing, Vec3(5, 5, 5));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    const fem::core::Mesh& mesh = result->mesh;
    CHECK(mesh.nodeCount() == 3 + 4 + 1);
    CHECK(mesh.elementCount(ElementType::Line2) == 2 + 4);
    CHECK(mesh.elementCount(ElementType::Point1) == 1);
    // The closing segment of the closed polyline ends on its first vertex.
    CHECK(elementNodeIds(mesh, 6) == std::vector<fem::core::ExternalId>{7, 4});
    // The point: the last node and the last element, in a group of
    // dimension 0.
    CHECK(nodeAt(mesh, 8) == Vec3(5, 5, 5));
    CHECK(elementNodeIds(mesh, 7) == std::vector<fem::core::ExternalId>{8});
    const auto* group = mesh.findGroup(fem::mesher::entityGroupName(point), 0);
    REQUIRE(group != nullptr);
    CHECK(group->elements.size() == 1);
    CHECK(mesh.findGroup(fem::mesher::entityGroupName(open), 1)->elements.size() == 2);
    CHECK(mesh.findGroup(fem::mesher::entityGroupName(closed), 1)->elements.size() == 4);
    // The map of the last segment of the closed polyline, and of the point.
    const auto closing = mesh.findElement(6);
    REQUIRE(closing.has_value());
    CHECK(result->map.elements[static_cast<std::size_t>(*closing)].entity == closed);
    CHECK(result->map.elements[static_cast<std::size_t>(*closing)].segment == 3);
    const auto pointNode = mesh.findNode(8);
    REQUIRE(pointNode.has_value());
    const auto& origin = result->map.nodes[static_cast<std::size_t>(*pointNode)];
    CHECK(origin.entities == std::vector<EntityId>{point});
    const auto vertex = origin.vertex.value_or(std::pair{EntityId{}, -1});
    CHECK(vertex.first == point);
    CHECK(vertex.second == 0);
}

TEST_CASE("a subset of the entities", "[mesher][line]")
{
    Frame f;
    drawFrame(f);
    // Given out of drawing order: meshed in drawing order.
    fem::mesher::LineMeshOptions options;
    options.entities = {f.beams[0], f.columns[0]};
    const auto result = meshLines(f.drawing, options);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 3);
    CHECK(result->mesh.elementCount() == 2);
    CHECK(elementNodeIds(result->mesh, 1) == std::vector<fem::core::ExternalId>{1, 2});
    CHECK(result->mesh.groups().size() == 2);
}

TEST_CASE("pieces shorter than the tolerance are ignored", "[mesher][line]")
{
    Drawing drawing;
    // A repeated vertex: the middle segment has no length.
    const EntityId id =
        addPolyline(drawing, {Vec3(0, 0, 0), Vec3(2, 0, 0), Vec3(2, 0, 0), Vec3(2, 2, 0)}, false);
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.elementCount() == 2);
    CHECK(result->mesh.nodeCount() == 3);
    REQUIRE(result->report.ignored.size() == 1);
    CHECK(result->report.ignored[0].first == id);
    CHECK(result->report.ignored[0].second == 1);
}

// REQ-009-14
TEST_CASE("drawings that cannot be meshed", "[mesher][line]")
{
    SECTION("empty")
    {
        const Drawing drawing;
        const auto result = meshLines(drawing);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::NoLines);
    }
    SECTION("only points")
    {
        Drawing drawing;
        addPoint(drawing, Vec3(0, 0, 0));
        addPoint(drawing, Vec3(1, 0, 0));
        const auto result = meshLines(drawing);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::NoLines);
    }
    SECTION("only a line of no length")
    {
        Drawing drawing;
        addLine(drawing, Vec3(1, 1, 1), Vec3(1, 1, 1));
        const auto result = meshLines(drawing);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::NoLines);
    }
    SECTION("an unknown entity")
    {
        Drawing drawing;
        const EntityId line = addLine(drawing, Vec3(0, 0, 0), Vec3(1, 0, 0));
        fem::mesher::LineMeshOptions options;
        options.entities = {line, EntityId{99}};
        const auto result = meshLines(drawing, options);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::UnknownEntity);
        CHECK(result.error().items == std::vector<EntityId>{EntityId{99}});
    }
}

namespace {

using fem::mesher::SplitKind;

// The positions of the nodes of a mesh, sorted, for comparisons that do not
// depend on the numbering.
std::vector<std::array<double, 3>> sortedNodes(const fem::core::Mesh& mesh)
{
    std::vector<std::array<double, 3>> out;
    for (fem::core::NodeIndex n = 0; n < mesh.nodeCount(); ++n) {
        const Vec3 p = mesh.nodeCoordinates(n);
        out.push_back({p.x(), p.y(), p.z()});
    }
    std::ranges::sort(out);
    return out;
}

bool hasNode(const fem::core::Mesh& mesh, const Vec3& p, double tolerance = 1e-12)
{
    for (fem::core::NodeIndex n = 0; n < mesh.nodeCount(); ++n) {
        if ((mesh.nodeCoordinates(n) - p).norm() <= tolerance) {
            return true;
        }
    }
    return false;
}

std::size_t groupSize(const fem::core::Mesh& mesh, EntityId id)
{
    const auto* group = mesh.findGroup(fem::mesher::entityGroupName(id), 1);
    return group == nullptr ? 0 : group->elements.size();
}

} // namespace

// T07 (AC-2): REQ-009-03, 11.
TEST_CASE("a beam ending on the middle of a column", "[mesher][line][contact]")
{
    Drawing drawing;
    const EntityId column = addLine(drawing, Vec3(0, 0, 0), Vec3(0, 6, 0));
    const EntityId beam = addLine(drawing, Vec3(0, 3, 0), Vec3(4, 3, 0));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    const auto& mesh = result->mesh;
    CHECK(mesh.nodeCount() == 4);
    CHECK(mesh.elementCount() == 3);
    // The column in two, through the node of the beam.
    CHECK(elementNodeIds(mesh, 1) == std::vector<fem::core::ExternalId>{1, 2});
    CHECK(elementNodeIds(mesh, 2) == std::vector<fem::core::ExternalId>{2, 3});
    CHECK(elementNodeIds(mesh, 3) == std::vector<fem::core::ExternalId>{2, 4});
    CHECK(nodeAt(mesh, 2) == Vec3(0, 3, 0));
    CHECK(groupSize(mesh, column) == 2);
    // Both elements of the column come from its only segment.
    for (const auto& origin : result->map.elements) {
        CHECK(origin.segment == 0);
    }
    REQUIRE(result->report.splits.size() == 1);
    const auto& split = result->report.splits[0];
    CHECK(split.kind == SplitKind::TJoint);
    CHECK(split.position == Vec3(0, 3, 0));
    CHECK(split.entities == std::vector<EntityId>{column, beam});
}

TEST_CASE("two bracing diagonals that cross", "[mesher][line][contact]")
{
    Drawing drawing;
    const EntityId first = addLine(drawing, Vec3(0, 0, 0), Vec3(4, 4, 0));
    const EntityId second = addLine(drawing, Vec3(4, 0, 0), Vec3(0, 4, 0));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 5);
    CHECK(result->mesh.elementCount() == 4);
    CHECK(nodeAt(result->mesh, 2) == Vec3(2, 2, 0));
    CHECK(elementNodeIds(result->mesh, 3) == std::vector<fem::core::ExternalId>{4, 2});
    REQUIRE(result->report.splits.size() == 1);
    CHECK(result->report.splits[0].kind == SplitKind::Crossing);
    CHECK(result->report.splits[0].entities == std::vector<EntityId>{first, second});
}

TEST_CASE("a line through a vertex of a polyline", "[mesher][line][contact]")
{
    Drawing drawing;
    const EntityId roof =
        addPolyline(drawing, {Vec3(0, 0, 0), Vec3(2, 2, 0), Vec3(4, 0, 0)}, false);
    const EntityId post = addLine(drawing, Vec3(2, 0, 0), Vec3(2, 4, 0));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    // The vertex is a node of both; only the line is split, once.
    CHECK(result->mesh.nodeCount() == 5);
    CHECK(result->mesh.elementCount() == 4);
    CHECK(groupSize(result->mesh, post) == 2);
    REQUIRE(result->report.splits.size() == 1);
    CHECK(result->report.splits[0].kind == SplitKind::TJoint);
    CHECK(result->report.splits[0].entities == std::vector<EntityId>{post, roof});
}

TEST_CASE("a point on a beam", "[mesher][line][contact]")
{
    Drawing drawing;
    const EntityId beam = addLine(drawing, Vec3(0, 0, 0), Vec3(4, 0, 0));
    const EntityId point = addPoint(drawing, Vec3(1, 0, 0));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 3);
    CHECK(result->mesh.elementCount(ElementType::Line2) == 2);
    CHECK(result->mesh.elementCount(ElementType::Point1) == 1);
    CHECK(nodeAt(result->mesh, 2) == Vec3(1, 0, 0));
    REQUIRE(result->report.splits.size() == 1);
    CHECK(result->report.splits[0].kind == SplitKind::PointOnLine);
    CHECK(result->report.splits[0].entities == std::vector<EntityId>{beam, point});
    // The node of the point lies on the beam, and comes from the point.
    const auto node = result->mesh.findNode(2);
    REQUIRE(node.has_value());
    const auto& origin = result->map.nodes[static_cast<std::size_t>(*node)];
    CHECK(origin.entities == std::vector<EntityId>{beam, point});
    CHECK(origin.vertex.value_or(std::pair{EntityId{}, -1}).first == point);
}

TEST_CASE("crossings in space, and lines that pass by", "[mesher][line][contact]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(2, 2, 2));
    addLine(drawing, Vec3(2, 0, 0), Vec3(0, 2, 2));
    // Skew, 2 apart.
    addLine(drawing, Vec3(10, 0, 0), Vec3(14, 0, 0));
    addLine(drawing, Vec3(11, -1, 2), Vec3(11, 1, 2));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 5 + 4);
    CHECK(result->mesh.elementCount() == 4 + 2);
    CHECK(hasNode(result->mesh, Vec3(1, 1, 1)));
    REQUIRE(result->report.splits.size() == 1);
    CHECK(result->report.splits[0].kind == SplitKind::Crossing);
}

TEST_CASE("many bars at one node", "[mesher][line][contact]")
{
    Drawing drawing;
    addLine(drawing, Vec3(-2, 0, 0), Vec3(2, 0, 0));
    addLine(drawing, Vec3(0, -2, 0), Vec3(0, 2, 0));
    addLine(drawing, Vec3(1, 1, 0), Vec3(0, 0, 0)); // ends on both
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 6);
    CHECK(result->mesh.elementCount() == 5);
    const std::vector<std::array<double, 3>> expected{{-2, 0, 0}, {0, -2, 0}, {0, 0, 0},
                                                      {0, 2, 0},  {1, 1, 0},  {2, 0, 0}};
    CHECK(sortedNodes(result->mesh) == expected);
}

TEST_CASE("a beam ending within the tolerance of a column", "[mesher][line][contact]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(0, 6, 0));
    // The tolerance is about 1e-8 here: 3e-9 away from the column.
    addLine(drawing, Vec3(3e-9, 3, 0), Vec3(4, 3, 0));
    const auto result = meshLines(drawing);
    REQUIRE(result.has_value());
    CHECK(result->mesh.nodeCount() == 4);
    CHECK(result->mesh.elementCount() == 3);
    // The node is the end of the beam, where it was drawn.
    CHECK(nodeAt(result->mesh, 2) == Vec3(3e-9, 3, 0));
}

// REQ-009-04
TEST_CASE("overlapping bars are refused", "[mesher][line][contact]")
{
    SECTION("two lines")
    {
        Drawing drawing;
        const EntityId first = addLine(drawing, Vec3(0, 0, 0), Vec3(3, 0, 0));
        const EntityId second = addLine(drawing, Vec3(2, 0, 0), Vec3(5, 0, 0));
        const auto result = meshLines(drawing);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::Overlaps);
        REQUIRE(result.error().overlaps.size() == 1);
        const auto& overlap = result.error().overlaps[0];
        CHECK(overlap.first == first);
        CHECK(overlap.second == second);
        CHECK(overlap.from == Vec3(2, 0, 0));
        CHECK(overlap.to == Vec3(3, 0, 0));
    }
    SECTION("a polyline folding back on itself")
    {
        Drawing drawing;
        const EntityId id =
            addPolyline(drawing, {Vec3(0, 0, 0), Vec3(3, 0, 0), Vec3(1, 0, 0)}, false);
        const auto result = meshLines(drawing);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(result.error().overlaps.size() == 1);
        CHECK(result.error().overlaps[0].first == id);
        CHECK(result.error().overlaps[0].second == id);
    }
}

namespace {

using fem::mesher::Grading;
using fem::mesher::LineMeshOptions;
using fem::mesher::LineSizing;

// The x of the nodes of a mesh along the x axis, sorted.
std::vector<double> nodeXs(const fem::core::Mesh& mesh)
{
    std::vector<double> xs;
    xs.reserve(static_cast<std::size_t>(mesh.nodeCount()));
    for (fem::core::NodeIndex n = 0; n < mesh.nodeCount(); ++n) {
        xs.push_back(mesh.nodeCoordinates(n).x());
    }
    std::ranges::sort(xs);
    return xs;
}

// The lengths of consecutive elements along x.
std::vector<double> lengthsOf(const std::vector<double>& xs)
{
    std::vector<double> out;
    for (std::size_t i = 1; i < xs.size(); ++i) {
        out.push_back(xs[i] - xs[i - 1]);
    }
    return out;
}

bool close(double a, double b)
{
    return std::abs(a - b) <= 1e-12 * std::max(1.0, std::abs(b));
}

fem::core::Mesh meshOf(const Drawing& drawing, const LineMeshOptions& options)
{
    auto result = meshLines(drawing, options);
    REQUIRE(result.has_value());
    return std::move(result->mesh);
}

} // namespace

// T09 (AC-4): REQ-009-05, 06, 07.
TEST_CASE("divisions and target lengths", "[mesher][line][sizing]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(8, 0, 0));
    LineMeshOptions options;

    SECTION("a number of divisions")
    {
        options.defaults.divisions = 4;
        const auto mesh = meshOf(drawing, options);
        CHECK(mesh.elementCount() == 4);
        CHECK(nodeXs(mesh) == std::vector<double>{0, 2, 4, 6, 8});
        // Numbered along the line.
        CHECK(nodeAt(mesh, 2) == Vec3(2, 0, 0));
        CHECK(elementNodeIds(mesh, 4) == std::vector<fem::core::ExternalId>{4, 5});
    }
    SECTION("a target length: ceil(L / h)")
    {
        options.defaults.length = 3.0;
        CHECK(meshOf(drawing, options).elementCount() == 3);
        options.defaults.length = 8.0;
        CHECK(meshOf(drawing, options).elementCount() == 1);
        options.defaults.length = 100.0;
        CHECK(meshOf(drawing, options).elementCount() == 1);
    }
    SECTION("divisions win over a length")
    {
        options.defaults.divisions = 2;
        options.defaults.length = 1.0;
        CHECK(meshOf(drawing, options).elementCount() == 2);
    }
}

TEST_CASE("a target length that divides the piece exactly", "[mesher][line][sizing]")
{
    LineMeshOptions options;
    options.defaults.length = 3.0;
    Drawing exact;
    addLine(exact, Vec3(0, 0, 0), Vec3(9, 0, 0));
    CHECK(meshOf(exact, options).elementCount() == 3);
    // L / h = 3 + 1e-12: still 3.
    Drawing nearly;
    addLine(nearly, Vec3(0, 0, 0), Vec3(9 + 3e-12, 0, 0));
    CHECK(meshOf(nearly, options).elementCount() == 3);
    // Clearly more: 4.
    Drawing more;
    addLine(more, Vec3(0, 0, 0), Vec3(9.1, 0, 0));
    CHECK(meshOf(more, options).elementCount() == 4);
}

TEST_CASE("graded divisions", "[mesher][line][sizing]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(15, 0, 0));
    LineMeshOptions options;

    SECTION("toward the end: 1, 2, 4, 8")
    {
        options.defaults.divisions = 4;
        options.defaults.ratio = 8.0;
        const auto lengths = lengthsOf(nodeXs(meshOf(drawing, options)));
        REQUIRE(lengths.size() == 4);
        CHECK(close(lengths[0], 1.0));
        CHECK(close(lengths[1], 2.0));
        CHECK(close(lengths[2], 4.0));
        CHECK(close(lengths[3], 8.0));
        CHECK(close(lengths[3] / lengths[0], 8.0));
    }
    SECTION("toward both ends, even: 1, 3, 3, 1")
    {
        options.defaults.divisions = 4;
        options.defaults.ratio = 3.0;
        options.defaults.grading = Grading::TowardBoth;
        const auto lengths = lengthsOf(nodeXs(meshOf(drawing, options)));
        REQUIRE(lengths.size() == 4);
        const double unit = 15.0 / 8.0;
        CHECK(close(lengths[0], unit));
        CHECK(close(lengths[1], 3 * unit));
        CHECK(close(lengths[2], 3 * unit));
        CHECK(close(lengths[3], unit));
    }
    SECTION("toward both ends, odd: 1, 2, 4, 2, 1")
    {
        options.defaults.divisions = 5;
        options.defaults.ratio = 4.0;
        options.defaults.grading = Grading::TowardBoth;
        const auto xs = nodeXs(meshOf(drawing, options));
        const auto lengths = lengthsOf(xs);
        REQUIRE(lengths.size() == 5);
        const double unit = 15.0 / 10.0;
        CHECK(close(lengths[0], unit));
        CHECK(close(lengths[1], 2 * unit));
        CHECK(close(lengths[2], 4 * unit));
        CHECK(close(lengths[3], 2 * unit));
        CHECK(close(lengths[4], unit));
        CHECK(xs.back() == 15.0);
    }
    SECTION("the sum is the piece")
    {
        options.defaults.divisions = 7;
        options.defaults.ratio = 0.37;
        const auto xs = nodeXs(meshOf(drawing, options));
        CHECK(xs.front() == 0.0);
        CHECK(xs.back() == 15.0);
        double sum = 0.0;
        for (const double l : lengthsOf(xs)) {
            sum += l;
        }
        CHECK(close(sum, 15.0));
    }
}

TEST_CASE("the grading follows the entity through a split", "[mesher][line][sizing]")
{
    Drawing drawing;
    const EntityId beam = addLine(drawing, Vec3(0, 0, 0), Vec3(8, 0, 0));
    addLine(drawing, Vec3(4, 0, 0), Vec3(4, 5, 0)); // a T at x = 4
    LineMeshOptions options;
    options.sizing[static_cast<std::uint64_t>(beam)] = {.divisions = 2, .ratio = 2.0};
    const auto mesh = meshOf(drawing, options);
    std::vector<double> xs;
    for (fem::core::NodeIndex n = 0; n < mesh.nodeCount(); ++n) {
        if (mesh.nodeCoordinates(n).y() == 0.0) {
            xs.push_back(mesh.nodeCoordinates(n).x());
        }
    }
    std::ranges::sort(xs);
    const auto lengths = lengthsOf(xs);
    REQUIRE(lengths.size() == 4);
    // Each half: 4/3 then 8/3, growing toward the end of the beam.
    CHECK(close(lengths[0], 4.0 / 3.0));
    CHECK(close(lengths[1], 8.0 / 3.0));
    CHECK(close(lengths[2], 4.0 / 3.0));
    CHECK(close(lengths[3], 8.0 / 3.0));
}

TEST_CASE("sizing per entity over the defaults", "[mesher][line][sizing]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(6, 0, 0));
    const EntityId second = addLine(drawing, Vec3(0, 5, 0), Vec3(6, 5, 0));
    LineMeshOptions options;
    options.defaults.divisions = 2;
    options.sizing[static_cast<std::uint64_t>(second)] = {.divisions = 3};
    const auto result = meshLines(drawing, options);
    REQUIRE(result.has_value());
    CHECK(result->mesh.elementCount() == 5);
    CHECK(result->mesh.findGroup(fem::mesher::entityGroupName(second), 1)->elements.size() == 3);
}

TEST_CASE("quadratic elements", "[mesher][line][sizing]")
{
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(4, 2, 0));
    LineMeshOptions options;
    options.defaults.divisions = 2;
    options.defaults.quadratic = true;
    const auto mesh = meshOf(drawing, options);
    CHECK(mesh.elementCount(ElementType::Line3) == 2);
    CHECK(mesh.elementCount(ElementType::Line2) == 0);
    CHECK(mesh.nodeCount() == 5);
    // Numbered along the line: start, middle, end; the middle node is the
    // midpoint of its element, and comes last in the element.
    CHECK(nodeAt(mesh, 2) == Vec3(1, 0.5, 0));
    CHECK(elementNodeIds(mesh, 1) == std::vector<fem::core::ExternalId>{1, 3, 2});
    CHECK(elementNodeIds(mesh, 2) == std::vector<fem::core::ExternalId>{3, 5, 4});
    CHECK(nodeAt(mesh, 4) == Vec3(3, 1.5, 0));
}

TEST_CASE("invalid sizing is refused", "[mesher][line][sizing]")
{
    Drawing drawing;
    const EntityId line = addLine(drawing, Vec3(0, 0, 0), Vec3(1, 0, 0));
    SECTION("per entity")
    {
        LineMeshOptions options;
        options.sizing[static_cast<std::uint64_t>(line)] = {.divisions = 0};
        const auto result = meshLines(drawing, options);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().kind == LineMeshError::Kind::InvalidSizing);
        CHECK(result.error().items == std::vector<EntityId>{line});
    }
    SECTION("the defaults")
    {
        for (const LineSizing& sizing :
             {LineSizing{.length = 0.0}, LineSizing{.length = -1.0}, LineSizing{.ratio = 0.0},
              LineSizing{.ratio = std::numeric_limits<double>::infinity()}}) {
            LineMeshOptions options;
            options.defaults = sizing;
            const auto result = meshLines(drawing, options);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error().kind == LineMeshError::Kind::InvalidSizing);
            CHECK(result.error().items.empty());
        }
    }
}
