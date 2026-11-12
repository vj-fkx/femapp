#include "mesh_fixtures.hpp"

#include <fem/render/camera.hpp>
#include <fem/render/mesh_picker.hpp>
#include <fem/render/mesh_scene.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <vector>

using fem::core::ElementType;
using fem::render::Camera;
using fem::render::DisplayOptions;
using fem::render::MeshPicker;
using fem::render::PickedItem;
using fem::render::PickRect;
using fem::render::Projection;
using fem::render::SelectionFilter;
using fem::render::SelectionKind;
using fem::render::StandardView;
using fem::render::Vec2;

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr double kTolerance = 5.0;

Camera cameraFor(const fem::core::Mesh& mesh, StandardView view,
                 Projection projection = Projection::Perspective)
{
    Camera camera;
    camera.setViewportSize(kWidth, kHeight);
    camera.setProjection(projection);
    camera.setStandardView(view);
    const fem::core::BoundingBox box = mesh.boundingBox();
    camera.fit({.min = box.min, .max = box.max});
    return camera;
}

Vec2 toPixel(const Camera& camera, const Eigen::Vector3d& world)
{
    const Eigen::Vector4d clip =
        camera.projectionMatrix() * camera.viewMatrix() * world.homogeneous();
    return {(clip.x() / clip.w() + 1.0) * 0.5 * kWidth,
            (1.0 - clip.y() / clip.w()) * 0.5 * kHeight};
}

SelectionFilter only(std::initializer_list<SelectionKind> kinds)
{
    SelectionFilter filter;
    filter.enabled = {false, false, false, false};
    for (const SelectionKind kind : kinds) {
        filter.enabled.at(fem::render::toIndex(kind)) = true;
    }
    return filter;
}

const SelectionFilter kElements = only({SelectionKind::Element});
const SelectionFilter kNodes = only({SelectionKind::Node});

// Index of a picked item of `kind`, or -1.
int indexOf(std::optional<PickedItem> item, SelectionKind kind)
{
    REQUIRE(item.has_value());
    const PickedItem picked = item.value_or(PickedItem{});
    CHECK(picked.kind == kind);
    return picked.kind == kind ? picked.index : -1;
}

int elementId(const fem::core::Mesh& mesh, std::optional<PickedItem> item)
{
    const int element = indexOf(item, SelectionKind::Element);
    return element < 0 ? -1 : static_cast<int>(mesh.elementId(element));
}

std::vector<int> ids(const fem::core::Mesh& mesh, const std::vector<int>& elements)
{
    std::vector<int> result;
    result.reserve(elements.size());
    for (const int e : elements) {
        result.push_back(static_cast<int>(mesh.elementId(e)));
    }
    std::ranges::sort(result);
    return result;
}

} // namespace

// REQ-004-20: clicking the middle of a 3 x 3 x 3 block picks the middle
// element of the layer facing the camera, in every standard view.
TEST_CASE("a click picks the front element", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));

    struct Case
    {
        StandardView view;
        std::array<int, 3> cell; // i, j, k of the expected element
    };
    const Case c = GENERATE(Case{.view = StandardView::Front, .cell = {1, 0, 1}},
                            Case{.view = StandardView::Back, .cell = {1, 2, 1}},
                            Case{.view = StandardView::Right, .cell = {2, 1, 1}},
                            Case{.view = StandardView::Left, .cell = {0, 1, 1}},
                            Case{.view = StandardView::Top, .cell = {1, 1, 2}},
                            Case{.view = StandardView::Bottom, .cell = {1, 1, 0}});
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const Camera camera = cameraFor(*mesh, c.view, projection);

    const auto picked = picker.pick(camera, Vec2(kWidth / 2.0, kHeight / 2.0), kElements,
                                    DisplayOptions{}, kTolerance);
    CHECK(elementId(*mesh, picked) == 1 + c.cell[0] + 3 * (c.cell[1] + 3 * c.cell[2]));
}

// REQ-004-20, REQ-004-16: hidden dimensions cannot be picked.
TEST_CASE("a click ignores hidden dimensions", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(1);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Front);
    DisplayOptions display;
    display.dimensionVisible[3] = false;
    CHECK_FALSE(
        picker.pick(camera, Vec2(kWidth / 2.0, kHeight / 2.0), kElements, display, kTolerance)
            .has_value());
}

// REQ-004-20: lines and points are picked within the tolerance.
TEST_CASE("a click picks lines and points within the tolerance", "[render][picker]")
{
    const auto mesh =
        fem::render::test::buildMesh({{0, 0, 0}, {2, 0, 0}, {1, 1, 0}},
                                     {{ElementType::Line2, {0, 1}}, {ElementType::Point1, {2}}});
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Top);

    const Vec2 onLine = toPixel(camera, {1.0, 0.0, 0.0});
    CHECK(elementId(*mesh, picker.pick(camera, onLine + Vec2(0, 3), kElements, DisplayOptions{},
                                       kTolerance)) == 1);
    CHECK_FALSE(picker.pick(camera, onLine + Vec2(0, 20), kElements, DisplayOptions{}, kTolerance)
                    .has_value());

    const Vec2 onPoint = toPixel(camera, {1.0, 1.0, 0.0});
    CHECK(elementId(*mesh, picker.pick(camera, onPoint + Vec2(2, -2), kElements, DisplayOptions{},
                                       kTolerance)) == 2);
}

// REQ-004-20: a node behind a face cannot be clicked; a visible one can.
TEST_CASE("a click picks visible nodes only", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(1);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    // Seen from the front, slightly from above and to the right.
    Camera camera = cameraFor(*mesh, StandardView::Front);
    camera.orbit(-20.0, 20.0);

    // Node 0 is (0,0,0), in front; node 7 is (0,1,1), at the back.
    const Vec2 front = toPixel(camera, {0, 0, 0});
    const auto pickedFront = picker.pick(camera, front, kNodes, DisplayOptions{}, kTolerance);
    REQUIRE(pickedFront.has_value());
    CHECK(mesh->nodeId(indexOf(pickedFront, SelectionKind::Node)) == 0);

    // Node 2 is (1,1,0): bottom, back right; hidden behind the front face.
    const Vec2 hidden = toPixel(camera, {1, 1, 0});
    const auto pickedHidden = picker.pick(camera, hidden, kNodes, DisplayOptions{}, kTolerance);
    if (pickedHidden.has_value()) {
        CHECK(mesh->nodeId(indexOf(pickedHidden, SelectionKind::Node)) != 2);
    }
}

// REQ-004-21, REQ-004-22: window and crossing on a 4 x 4 quad grid seen from above.
TEST_CASE("window and crossing select by inclusion and by touch", "[render][picker]")
{
    const auto mesh = fem::render::test::quadGrid(4);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Top, Projection::Orthographic);

    const Vec2 a = toPixel(camera, {0.5, 0.5, 0.0});
    const Vec2 b = toPixel(camera, {2.5, 2.5, 0.0});
    const Vec2 left(std::min(a.x(), b.x()), 0.0);
    const Vec2 right(std::max(a.x(), b.x()), 0.0);
    const Vec2 top(0.0, std::min(a.y(), b.y()));
    const Vec2 bottom(0.0, std::max(a.y(), b.y()));

    // Window: left to right. Only quad (1, 1) lies entirely inside.
    const PickRect window{.from = Vec2(left.x(), top.y()), .to = Vec2(right.x(), bottom.y())};
    CHECK(ids(*mesh, picker.pickRect(camera, window, kElements,
                                     DisplayOptions{})[SelectionKind::Element]) ==
          std::vector<int>{1 + 1 + 4 * 1});

    // Crossing: right to left. The 3 x 3 quads touched by the rectangle.
    const PickRect crossing{.from = Vec2(right.x(), top.y()), .to = Vec2(left.x(), bottom.y())};
    std::vector<int> touched;
    for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
            touched.push_back(1 + i + 4 * j);
        }
    }
    std::ranges::sort(touched);
    CHECK(ids(*mesh, picker.pickRect(camera, crossing, kElements,
                                     DisplayOptions{})[SelectionKind::Element]) == touched);

    // Nodes inside: (1,1), (2,1), (1,2), (2,2).
    const auto nodes =
        picker.pickRect(camera, window, kNodes, DisplayOptions{})[SelectionKind::Node];
    std::vector<int> nodeIds;
    nodeIds.reserve(nodes.size());
    for (const int n : nodes) {
        nodeIds.push_back(static_cast<int>(mesh->nodeId(n)));
    }
    std::ranges::sort(nodeIds);
    CHECK(nodeIds == std::vector<int>{6, 7, 11, 12});
}

// REQ-004-22: a small crossing rectangle inside a large element selects it.
TEST_CASE("crossing inside one element selects it", "[render][picker]")
{
    const auto mesh = fem::render::test::quadGrid(1);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Top, Projection::Orthographic);
    const Vec2 c = toPixel(camera, {0.5, 0.5, 0.0});
    const PickRect crossing{.from = c + Vec2(3, -3), .to = c + Vec2(-3, 3)};
    const PickRect window{.from = c + Vec2(-3, -3), .to = c + Vec2(3, 3)};
    CHECK(picker.pickRect(camera, crossing, kElements, DisplayOptions{})[SelectionKind::Element]
              .size() == 1);
    CHECK(picker.pickRect(camera, window, kElements, DisplayOptions{})[SelectionKind::Element]
              .empty());
}

// REQ-004-21: a window around a block selects the hidden inner elements too,
// but not the hidden dimensions.
TEST_CASE("window includes hidden elements of visible dimensions", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Front);
    const PickRect window{.from = Vec2(1, 1), .to = Vec2(kWidth - 1, kHeight - 1)};
    CHECK(picker.pickRect(camera, window, kElements, DisplayOptions{})[SelectionKind::Element]
              .size() == 27);

    DisplayOptions noSolids;
    noSolids.dimensionVisible[3] = false;
    CHECK(picker.pickRect(camera, window, kElements, noSolids)[SelectionKind::Element].empty());
}

// REQ-002-13 (changed in feature 004): the orbit pivot is the visible mesh
// point at the centre of the screen.
TEST_CASE("surface point under the centre of the screen", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const Camera camera = cameraFor(*mesh, StandardView::Front, projection);
    const Vec2 centre(kWidth / 2.0, kHeight / 2.0);

    const auto point = picker.surfacePoint(camera, centre, DisplayOptions{});
    REQUIRE(point.has_value());
    const Eigen::Vector3d p = point.value_or(Eigen::Vector3d::Zero());
    CHECK(std::abs(p.y()) < 1e-5);       // the front face, y = 0
    CHECK(std::abs(p.x() - 1.5) < 1e-5); // in the middle of the block
    CHECK(std::abs(p.z() - 1.5) < 1e-5);

    DisplayOptions hidden;
    hidden.dimensionVisible[3] = false;
    CHECK_FALSE(picker.surfacePoint(camera, centre, hidden).has_value());
}

namespace {

Eigen::Vector3d nodeAt(const fem::core::Mesh& mesh, fem::core::NodeIndex node)
{
    return mesh.nodeCoordinates(node);
}

} // namespace

// REQ-004-20, REQ-004-32: a click picks by priority element, face, edge, node.
TEST_CASE("a click picks solid faces, edges and nodes by priority", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const auto scene = fem::render::buildMeshScene(mesh);
    const MeshPicker picker(scene);
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const Camera camera = cameraFor(*mesh, StandardView::Front, projection);
    const Vec2 centre(kWidth / 2.0, kHeight / 2.0);
    const auto& faces = scene->surface().faces3d;
    const auto& edges = scene->surface().edges3d;

    SECTION("faces only: the middle face of the front")
    {
        const int f = indexOf(
            picker.pick(camera, centre, only({SelectionKind::Face}), DisplayOptions{}, kTolerance),
            SelectionKind::Face);
        REQUIRE(f >= 0);
        const auto& face = faces[static_cast<std::size_t>(f)];
        CHECK(mesh->elementId(face.element) == 1 + 1 + 3 * (0 + 3 * 1));
        for (std::size_t v = 0; v < face.nodeCount; ++v) {
            CHECK(nodeAt(*mesh, face.nodes.at(v)).y() == 0.0);
        }
    }
    SECTION("all kinds: the element wins")
    {
        const SelectionFilter all;
        CHECK(elementId(*mesh, picker.pick(camera, centre, all, DisplayOptions{}, kTolerance)) ==
              1 + 1 + 3 * (0 + 3 * 1));
    }
    SECTION("faces, edges and nodes: the face wins")
    {
        const auto picked = picker.pick(
            camera, centre, only({SelectionKind::Face, SelectionKind::Edge, SelectionKind::Node}),
            DisplayOptions{}, kTolerance);
        CHECK(indexOf(picked, SelectionKind::Face) >= 0);
    }
    SECTION("elements only: the element")
    {
        CHECK(elementId(*mesh, picker.pick(camera, centre, kElements, DisplayOptions{},
                                           kTolerance)) == 1 + 1 + 3 * (0 + 3 * 1));
    }
    SECTION("edges: the front edge near the cursor")
    {
        const Vec2 near = toPixel(camera, {1.5, 0.0, 1.0}) + Vec2(0.0, 2.0);
        const auto filter = only({SelectionKind::Edge, SelectionKind::Node});
        const int e = indexOf(picker.pick(camera, near, filter, DisplayOptions{}, kTolerance),
                              SelectionKind::Edge);
        REQUIRE(e >= 0);
        const auto& edge = edges[static_cast<std::size_t>(e)];
        const Eigen::Vector3d a = nodeAt(*mesh, edge.a);
        const Eigen::Vector3d b = nodeAt(*mesh, edge.b);
        CHECK((0.5 * (a + b) - Eigen::Vector3d(1.5, 0.0, 1.0)).norm() < 1e-9);
    }
    SECTION("edges and nodes at a vertex: an edge wins")
    {
        const Vec2 vertex = toPixel(camera, {1.0, 0.0, 1.0}) + Vec2(1.0, -1.0);
        const auto picked =
            picker.pick(camera, vertex, only({SelectionKind::Node, SelectionKind::Edge}),
                        DisplayOptions{}, kTolerance);
        CHECK(indexOf(picked, SelectionKind::Edge) >= 0);
    }
    SECTION("nodes only: the node at a vertex")
    {
        const Vec2 vertex = toPixel(camera, {1.0, 0.0, 1.0}) + Vec2(1.0, -1.0);
        const int n = indexOf(picker.pick(camera, vertex, kNodes, DisplayOptions{}, kTolerance),
                              SelectionKind::Node);
        REQUIRE(n >= 0);
        CHECK((nodeAt(*mesh, n) - Eigen::Vector3d(1.0, 0.0, 1.0)).norm() < 1e-9);
    }
    SECTION("an edge of the back is hidden")
    {
        const Vec2 back = toPixel(camera, {1.5, 3.0, 1.0});
        const auto picked =
            picker.pick(camera, back, only({SelectionKind::Edge}), DisplayOptions{}, kTolerance);
        if (picked.has_value()) {
            const auto& edge = edges[static_cast<std::size_t>(picked->index)];
            CHECK(nodeAt(*mesh, edge.a).y() == 0.0);
            CHECK(nodeAt(*mesh, edge.b).y() == 0.0);
        }
    }
    SECTION("hidden solids: nothing")
    {
        DisplayOptions hidden;
        hidden.dimensionVisible[3] = false;
        CHECK_FALSE(picker
                        .pick(camera, centre, only({SelectionKind::Face, SelectionKind::Edge}),
                              hidden, kTolerance)
                        .has_value());
    }
}

// REQ-004-21, REQ-004-22, REQ-004-32: window and crossing with faces and edges.
TEST_CASE("window and crossing select solid faces and edges", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(1);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const Camera camera = cameraFor(*mesh, StandardView::Front, projection);
    const auto filter = only({SelectionKind::Face, SelectionKind::Edge});

    const PickRect all{.from = Vec2(1, 1), .to = Vec2(kWidth - 1, kHeight - 1)};
    const auto everything = picker.pickRect(camera, all, filter, DisplayOptions{});
    CHECK(everything[SelectionKind::Face].size() == 6);
    CHECK(everything[SelectionKind::Edge].size() == 12);
    CHECK(everything[SelectionKind::Element].empty());
    CHECK(everything[SelectionKind::Node].empty());

    // A small crossing in the middle: the front face and, hidden behind it,
    // the back face; no edge.
    const Vec2 c(kWidth / 2.0, kHeight / 2.0);
    const PickRect crossing{.from = c + Vec2(3, -3), .to = c + Vec2(-3, 3)};
    const auto middle = picker.pickRect(camera, crossing, filter, DisplayOptions{});
    CHECK(middle[SelectionKind::Face].size() == 2);
    CHECK(middle[SelectionKind::Edge].empty());

    DisplayOptions hidden;
    hidden.dimensionVisible[3] = false;
    CHECK(picker.pickRect(camera, all, filter, hidden).empty());
}

// REQ-004-20 to 22, 32: the edges of 2D elements are selectable too.
TEST_CASE("edges of 2D elements are picked by click and rectangle", "[render][picker]")
{
    const auto mesh = fem::render::test::quadGrid(2);
    const auto scene = fem::render::buildMeshScene(mesh);
    const MeshPicker picker(scene);
    const Camera camera = cameraFor(*mesh, StandardView::Top, Projection::Orthographic);
    const auto edgesOnly = only({SelectionKind::Edge});

    // Near the middle of the edge from (1, 0) to (1, 1).
    const Vec2 near = toPixel(camera, {1.0, 0.5, 0.0}) + Vec2(2.0, 0.0);
    const int e = indexOf(picker.pick(camera, near, edgesOnly, DisplayOptions{}, kTolerance),
                          SelectionKind::Edge);
    REQUIRE(e >= 0);
    CHECK(scene->edgeDimension(e) == 2);
    const Eigen::Vector3d a = mesh->nodeCoordinates(scene->edge(e).a);
    const Eigen::Vector3d b = mesh->nodeCoordinates(scene->edge(e).b);
    CHECK((0.5 * (a + b) - Eigen::Vector3d(1.0, 0.5, 0.0)).norm() < 1e-9);

    // A window around everything: the 12 edges of the 2 x 2 grid.
    const PickRect all{.from = Vec2(1, 1), .to = Vec2(kWidth - 1, kHeight - 1)};
    CHECK(picker.pickRect(camera, all, edgesOnly, DisplayOptions{})[SelectionKind::Edge].size() ==
          12);

    DisplayOptions hidden;
    hidden.dimensionVisible[2] = false;
    CHECK_FALSE(picker.pick(camera, near, edgesOnly, hidden, kTolerance).has_value());
    CHECK(picker.pickRect(camera, all, edgesOnly, hidden).empty());
}

// REQ-002-14 (changed in feature 004): zooming in on the mesh under the
// cursor never goes through it, so a click there still picks the element.
TEST_CASE("zooming in on the mesh keeps it pickable", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = cameraFor(*mesh, StandardView::Isometric, projection);
    // The middle of the top face; the work plane (z = 0) is below the block.
    const Vec2 cursor = toPixel(camera, {1.5, 1.5, 3.0});
    for (int step = 0; step < 20; ++step) {
        camera.zoomAt(cursor, 3.0, picker.surfacePoint(camera, cursor, DisplayOptions{}));
    }
    CHECK(camera.position().z() > 3.0); // still above the top face
    CHECK((toPixel(camera, {1.5, 1.5, 3.0}) - cursor).norm() < 1e-3);
    CHECK(elementId(*mesh, picker.pick(camera, cursor, kElements, DisplayOptions{}, kTolerance)) ==
          1 + 1 + 3 * (1 + 3 * 2));
}

// REQ-004-20: a 2D boundary triangle lying on a solid face (as Gmsh writes
// physical surfaces) does not hide the solid from a click.
TEST_CASE("a click on a solid face covered by a 2D triangle picks the solid", "[render][picker]")
{
    const auto mesh = fem::render::test::buildMesh(
        {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        {{ElementType::Tri3, {0, 1, 3}}, {ElementType::Tet4, {0, 1, 2, 3}}});
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const Camera camera = cameraFor(*mesh, StandardView::Front, projection);
    // The face y = 0 of the tetrahedron, where the triangle lies too.
    const Vec2 onFace = toPixel(camera, {0.25, 0.0, 0.25});
    CHECK(elementId(*mesh, picker.pick(camera, onFace, kElements, DisplayOptions{}, kTolerance)) ==
          2);

    // With the solids hidden, the triangle is picked.
    DisplayOptions noSolids;
    noSolids.dimensionVisible[3] = false;
    CHECK(elementId(*mesh, picker.pick(camera, onFace, kElements, noSolids, kTolerance)) == 1);
}
