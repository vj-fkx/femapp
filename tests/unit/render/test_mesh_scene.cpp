#include "mesh_fixtures.hpp"

#include <fem/core/element_type.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>

using Catch::Matchers::WithinAbs;
using fem::core::ElementType;
using fem::render::ColorMode;
using fem::render::LineSet;
using fem::render::MeshScene;
using fem::render::Rgba;

namespace {

std::shared_ptr<const fem::core::Mesh> mixedMesh()
{
    // A hexahedron, a quadrilateral and a triangle beside it, a line and a point.
    const std::vector<Eigen::Vector3d> nodes = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1},
        {1, 1, 1}, {0, 1, 1}, {2, 0, 0}, {2, 1, 0}, {3, 0, 0}, {4, 0, 0},
    };
    return fem::render::test::buildMesh(nodes,
                                        {
                                            {ElementType::Hex8, {0, 1, 2, 3, 4, 5, 6, 7}},
                                            {ElementType::Quad4, {1, 8, 9, 2}},
                                            {ElementType::Tri3, {8, 10, 9}},
                                            {ElementType::Line2, {10, 11}},
                                            {ElementType::Point1, {11}},
                                        },
                                        {{"plates", {1, 2}}, {"plate", {1}}, {"bar", {3}}});
}

} // namespace

// REQ-004-09, REQ-004-16: draw streams and their ranges per dimension.
TEST_CASE("scene splits primitives by dimension", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(mixedMesh());

    // 2D: quad (2 triangles) + triangle; 3D: 6 hexahedron faces (12 triangles).
    CHECK(scene->triangles(2).first == 0);
    CHECK(scene->triangles(2).count == 3);
    CHECK(scene->triangles(3).first == 3);
    CHECK(scene->triangles(3).count == 12);
    CHECK(scene->triangleVertices().size() == std::size_t{3} * 15);
    CHECK(scene->triangleElements().size() == 15);

    // 1D element, 2D edges (4 + 3 - 1 shared), 3D edges (12).
    CHECK(scene->lines(LineSet::Elements1d).count == 1);
    CHECK(scene->lines(LineSet::Edges2d).count == 6);
    CHECK(scene->lines(LineSet::Edges3d).count == 12);
    CHECK(scene->lines(LineSet::Edges3d).first == 7);
    CHECK(scene->lineVertices().size() == std::size_t{2} * 19);

    CHECK(scene->pointElements().size() == 1);
    CHECK(scene->surfaceNodes(0).size() == 1);
    CHECK(scene->surfaceNodes(1).size() == 2);
    CHECK(scene->surfaceNodes(2).size() == 5);
    CHECK(scene->surfaceNodes(3).size() == 8);
    CHECK(scene->allNodes(3).size() == 8);
    CHECK(scene->surfaceNodes(4).empty());
}

// REQ-004-18: positions are stored relative to the centre of the mesh.
TEST_CASE("scene positions are relative to the mesh centre", "[render][scene]")
{
    const Eigen::Vector3d shift(1.0e6, -2.0e6, 3.0e6);
    const auto scene = fem::render::buildMeshScene(fem::render::test::hexGrid(2, shift));
    CHECK((scene->origin() - (shift + Eigen::Vector3d(1, 1, 1))).norm() < 1e-9);
    for (const Eigen::Vector3f& p : scene->triangleVertices()) {
        CHECK(p.cwiseAbs().maxCoeff() <= 1.0F);
        // Unit coordinates survive exactly despite the large offset.
        CHECK(p.x() == std::round(p.x()));
    }
}

TEST_CASE("scene of an empty mesh", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(fem::render::test::buildMesh({}, {}));
    CHECK(scene->triangleVertices().empty());
    CHECK(scene->lineVertices().empty());
    CHECK(scene->bounds().isEmpty());
    CHECK(scene->elementColors(ColorMode::ByGroup).empty());
}

// REQ-004-14, REQ-004-15
TEST_CASE("element colors by dimension and by group", "[render][scene][color]")
{
    const auto mesh = mixedMesh();
    const auto scene = fem::render::buildMeshScene(mesh);
    const auto element = [&](fem::core::ExternalId id) {
        return static_cast<std::size_t>(mesh->findElement(id).value_or(-1));
    };

    const std::vector<Rgba> byDimension = scene->elementColors(ColorMode::ByDimension);
    CHECK(byDimension[element(1)] == fem::render::dimensionColor(3));
    CHECK(byDimension[element(2)] == fem::render::dimensionColor(2));
    CHECK(byDimension[element(3)] == fem::render::dimensionColor(2));
    CHECK(byDimension[element(4)] == fem::render::dimensionColor(1));
    CHECK(byDimension[element(5)] == fem::render::dimensionColor(0));

    const std::vector<Rgba> byGroup = scene->elementColors(ColorMode::ByGroup);
    // "plates" is group 0 and contains elements 2 and 3; element 2 is also in
    // "plate" (group 1) but keeps the color of the first group.
    CHECK(byGroup[element(2)] == fem::render::groupColor(0));
    CHECK(byGroup[element(3)] == fem::render::groupColor(0));
    CHECK(byGroup[element(4)] == fem::render::groupColor(2));
    CHECK(byGroup[element(1)] == fem::render::kNoGroupColor);
    CHECK(byGroup[element(5)] == fem::render::kNoGroupColor);

    // The palette repeats after twelve groups and its colors are distinct.
    CHECK(fem::render::groupColor(12) == fem::render::groupColor(0));
    std::set<Rgba> palette;
    for (std::size_t g = 0; g < 12; ++g) {
        palette.insert(fem::render::groupColor(g));
    }
    CHECK(palette.size() == 12);
}

// REQ-004-32: each outer face of a block of hexahedra gives two triangles,
// and the 3D segments follow the order of the outer edges.
TEST_CASE("scene maps triangles to faces and segments to edges", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(fem::render::test::hexGrid(3));
    const auto& faces = scene->surface().faces3d;
    CHECK(faces.size() == 54);
    const fem::render::Range solids = scene->triangles(3);
    CHECK(solids.count == 108);
    const auto triangleFaces = scene->triangleFaces();
    REQUIRE(triangleFaces.size() == scene->triangleElements().size());
    std::vector<int> perFace(faces.size(), 0);
    for (std::uint32_t t = solids.first; t < solids.first + solids.count; ++t) {
        const std::uint32_t f = triangleFaces[t];
        REQUIRE(f < faces.size());
        ++perFace[f];
        CHECK(scene->triangleElements()[t] == faces[f].element);
    }
    CHECK(std::ranges::all_of(perFace, [](int n) { return n == 2; }));

    const fem::render::Range edges = scene->lines(LineSet::Edges3d);
    REQUIRE(edges.count == scene->surface().edges3d.size());
    const auto positions = scene->nodePositions();
    for (std::uint32_t s = 0; s < edges.count; ++s) {
        const auto& edge = scene->surface().edges3d[s];
        const std::size_t v = 2 * static_cast<std::size_t>(edges.first + s);
        CHECK(scene->lineVertices()[v] == positions[static_cast<std::size_t>(edge.a)]);
        CHECK(scene->lineVertices()[v + 1] == positions[static_cast<std::size_t>(edge.b)]);
    }
}

// REQ-004-24, REQ-004-25, REQ-004-28, REQ-004-33
TEST_CASE("selection adds, removes, clears and drops a dimension", "[render][selection]")
{
    using fem::render::SelectionKind;
    const auto mesh = mixedMesh();
    const auto scene = fem::render::buildMeshScene(mesh);
    fem::render::Selection selection;
    selection.reset(scene.get());
    CHECK(selection.total() == 0);

    fem::render::SelectionItems all;
    all[SelectionKind::Element] = {0, 1, 2, 3, 4};
    all[SelectionKind::Face] = {0, 5};
    all[SelectionKind::Edge] = {0, 11};
    all[SelectionKind::Node] = {0, 11};
    selection.add(all);
    selection.add(all); // twice: no change
    CHECK(selection.count(SelectionKind::Element) == 5);
    CHECK(selection.count(SelectionKind::Face) == 2);
    CHECK(selection.count(SelectionKind::Edge) == 2);
    CHECK(selection.count(SelectionKind::Node) == 2);
    CHECK(selection.total() == 11);

    // Out of range indices are ignored: 6 faces and 6 + 12 edges (2D first:
    // edge 0 is a 2D edge, edge 11 a solid one).
    fem::render::SelectionItems some;
    some[SelectionKind::Element] = {1, 4, 99, -1};
    some[SelectionKind::Face] = {6};
    some[SelectionKind::Edge] = {18};
    selection.remove(some);
    CHECK(selection.count(SelectionKind::Element) == 3);
    CHECK_FALSE(selection.contains(SelectionKind::Element, 1));
    CHECK(selection.contains(SelectionKind::Element, 0));
    CHECK(selection.count(SelectionKind::Face) == 2);
    CHECK(selection.items(SelectionKind::Edge) == std::vector<int>{0, 11});

    // Internal order follows the element types: point, line, triangle,
    // quadrilateral, hexahedron. Left: point, triangle and quadrilateral.
    // Hiding 2D takes out the triangle, the quadrilateral and the 2D edge,
    // not the solid faces and edges.
    const int point = mesh->findElement(5).value_or(-1);
    const int triangle = mesh->findElement(3).value_or(-1);
    REQUIRE(selection.contains(SelectionKind::Element, triangle));
    selection.removeElementsOfDimension(*scene, 2);
    CHECK_FALSE(selection.contains(SelectionKind::Element, triangle));
    CHECK(selection.items(SelectionKind::Element) == std::vector<int>{point});
    CHECK(selection.count(SelectionKind::Face) == 2);
    CHECK(selection.items(SelectionKind::Edge) == std::vector<int>{11});

    // Hiding 3D takes out the solid faces and edges; nodes stay.
    selection.removeElementsOfDimension(*scene, 3);
    CHECK(selection.count(SelectionKind::Face) == 0);
    CHECK(selection.count(SelectionKind::Edge) == 0);
    CHECK(selection.count(SelectionKind::Node) == 2);

    // REQ-004-33: clearing one kind keeps the others.
    selection.clear(SelectionKind::Node);
    CHECK(selection.count(SelectionKind::Node) == 0);
    CHECK(selection.count(SelectionKind::Element) == 1);

    selection.clear();
    CHECK(selection.total() == 0);

    selection.reset(nullptr);
    selection.add(all);
    CHECK(selection.total() == 0);
}

// REQ-004-32: edges are numbered 2D first, then solid, like the segments.
TEST_CASE("scene numbers 2D and solid edges like the segments", "[render][scene]")
{
    const auto scene = fem::render::buildMeshScene(mixedMesh());
    REQUIRE(scene->edgeCount() == 6 + 12);
    const std::uint32_t first = scene->lines(LineSet::Edges2d).first;
    const auto positions = scene->nodePositions();
    for (int e = 0; e < scene->edgeCount(); ++e) {
        CHECK(scene->edgeDimension(e) == (e < 6 ? 2 : 3));
        const std::size_t v = 2 * static_cast<std::size_t>(first + static_cast<std::uint32_t>(e));
        CHECK(scene->lineVertices()[v] == positions[static_cast<std::size_t>(scene->edge(e).a)]);
        CHECK(scene->lineVertices()[v + 1] ==
              positions[static_cast<std::size_t>(scene->edge(e).b)]);
    }
}

namespace {

// Elements of `type` placed at their reference coordinates, shifted along x
// by 2 per element; nodes at the same position are shared.
std::shared_ptr<const fem::core::Mesh> referenceElements(ElementType type, int count)
{
    std::vector<Eigen::Vector3d> nodes;
    fem::render::test::ElementList elements;
    for (int i = 0; i < count; ++i) {
        std::vector<fem::core::ExternalId> ids;
        for (const auto& p : fem::core::referenceNodeCoordinates(type)) {
            const Eigen::Vector3d x(p[0] + 2.0 * i, p[1], p[2]);
            const auto same = std::ranges::find_if(
                nodes, [&](const Eigen::Vector3d& y) { return (x - y).norm() < 1e-12; });
            if (same == nodes.end()) {
                ids.push_back(static_cast<fem::core::ExternalId>(nodes.size()));
                nodes.push_back(x);
            } else {
                ids.push_back(static_cast<fem::core::ExternalId>(same - nodes.begin()));
            }
        }
        elements.emplace_back(type, ids);
    }
    return fem::render::test::buildMesh(nodes, elements);
}

} // namespace

// REQ-004-34: the higher order nodes on the outer faces are shown, those
// inside the solid are not.
TEST_CASE("surface nodes include the higher order nodes on the outer faces", "[render][scene]")
{
    CHECK(fem::render::buildMeshScene(referenceElements(ElementType::Tet10, 1))
              ->surfaceNodes(3)
              .size() == 10);
    // All but the centre node.
    CHECK(fem::render::buildMeshScene(referenceElements(ElementType::Hex27, 1))
              ->surfaceNodes(3)
              .size() == 26);
    // Two hexahedra sharing a face (Gmsh's reference is [-1, 1]^3): 45 nodes;
    // the two centres and the centre of the shared face are inside.
    const auto pair = referenceElements(ElementType::Hex27, 2);
    REQUIRE(pair->nodeCount() == 45);
    CHECK(fem::render::buildMeshScene(pair)->surfaceNodes(3).size() == 42);

    // 2D and 1D elements show all their nodes.
    const auto quad = fem::render::buildMeshScene(referenceElements(ElementType::Quad9, 1));
    CHECK(quad->surfaceNodes(2).size() == 9);
}

// REQ-004-36: flat meshes without solids are seen straight on.
TEST_CASE("planar meshes get the view normal to their plane", "[render][scene]")
{
    using fem::render::StandardView;
    const auto viewOf = [](const std::vector<Eigen::Vector3d>& nodes, ElementType type,
                           const std::vector<fem::core::ExternalId>& ids) {
        return fem::render::planarView(
            *fem::render::buildMeshScene(fem::render::test::buildMesh(nodes, {{type, ids}})));
    };
    CHECK(viewOf({{0, 0, 5}, {2, 0, 5}, {2, 1, 5}, {0, 1, 5}}, ElementType::Quad4, {0, 1, 2, 3}) ==
          StandardView::Top);
    CHECK(viewOf({{0, 3, 0}, {2, 3, 0}, {2, 3, 1}, {0, 3, 1}}, ElementType::Quad4, {0, 1, 2, 3}) ==
          StandardView::Front);
    CHECK(viewOf({{4, 0, 0}, {4, 2, 0}, {4, 2, 1}, {4, 0, 1}}, ElementType::Quad4, {0, 1, 2, 3}) ==
          StandardView::Right);
    // A tilted plate and a solid keep the current view.
    CHECK_FALSE(
        viewOf({{0, 0, 0}, {1, 0, 0}, {1, 1, 1}}, ElementType::Tri3, {0, 1, 2}).has_value());
    CHECK_FALSE(fem::render::planarView(*fem::render::buildMeshScene(mixedMesh())).has_value());
    // A planar bar structure counts too.
    CHECK(viewOf({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}}, ElementType::Line2, {0, 2}) ==
          StandardView::Front);
}
