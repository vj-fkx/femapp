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
#include <optional>
#include <vector>

using fem::core::ElementType;
using fem::render::Camera;
using fem::render::DisplayOptions;
using fem::render::MeshPicker;
using fem::render::PickRect;
using fem::render::Projection;
using fem::render::SelectionMode;
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

int elementId(const fem::core::Mesh& mesh, std::optional<int> element)
{
    REQUIRE(element.has_value());
    return static_cast<int>(mesh.elementId(element.value_or(0)));
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

    const auto picked = picker.pick(camera, Vec2(kWidth / 2.0, kHeight / 2.0),
                                    SelectionMode::Elements, DisplayOptions{}, kTolerance);
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
    CHECK_FALSE(picker
                    .pick(camera, Vec2(kWidth / 2.0, kHeight / 2.0), SelectionMode::Elements,
                          display, kTolerance)
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
    CHECK(elementId(*mesh, picker.pick(camera, onLine + Vec2(0, 3), SelectionMode::Elements,
                                       DisplayOptions{}, kTolerance)) == 1);
    CHECK_FALSE(picker
                    .pick(camera, onLine + Vec2(0, 20), SelectionMode::Elements, DisplayOptions{},
                          kTolerance)
                    .has_value());

    const Vec2 onPoint = toPixel(camera, {1.0, 1.0, 0.0});
    CHECK(elementId(*mesh, picker.pick(camera, onPoint + Vec2(2, -2), SelectionMode::Elements,
                                       DisplayOptions{}, kTolerance)) == 2);
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
    const auto pickedFront =
        picker.pick(camera, front, SelectionMode::Nodes, DisplayOptions{}, kTolerance);
    REQUIRE(pickedFront.has_value());
    CHECK(mesh->nodeId(pickedFront.value_or(0)) == 0);

    // Node 2 is (1,1,0): bottom, back right; hidden behind the front face.
    const Vec2 hidden = toPixel(camera, {1, 1, 0});
    const auto pickedHidden =
        picker.pick(camera, hidden, SelectionMode::Nodes, DisplayOptions{}, kTolerance);
    if (pickedHidden.has_value()) {
        CHECK(mesh->nodeId(pickedHidden.value_or(0)) != 2);
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
    CHECK(ids(*mesh, picker.pickRect(camera, window, SelectionMode::Elements, DisplayOptions{})) ==
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
    CHECK(ids(*mesh, picker.pickRect(camera, crossing, SelectionMode::Elements,
                                     DisplayOptions{})) == touched);

    // Nodes inside: (1,1), (2,1), (1,2), (2,2).
    const auto nodes = picker.pickRect(camera, window, SelectionMode::Nodes, DisplayOptions{});
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
    CHECK(picker.pickRect(camera, crossing, SelectionMode::Elements, DisplayOptions{}).size() == 1);
    CHECK(picker.pickRect(camera, window, SelectionMode::Elements, DisplayOptions{}).empty());
}

// REQ-004-21: a window around a block selects the hidden inner elements too,
// but not the hidden dimensions.
TEST_CASE("window includes hidden elements of visible dimensions", "[render][picker]")
{
    const auto mesh = fem::render::test::hexGrid(3);
    const MeshPicker picker(fem::render::buildMeshScene(mesh));
    const Camera camera = cameraFor(*mesh, StandardView::Front);
    const PickRect window{.from = Vec2(1, 1), .to = Vec2(kWidth - 1, kHeight - 1)};
    CHECK(picker.pickRect(camera, window, SelectionMode::Elements, DisplayOptions{}).size() == 27);

    DisplayOptions noSolids;
    noSolids.dimensionVisible[3] = false;
    CHECK(picker.pickRect(camera, window, SelectionMode::Elements, noSolids).empty());
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
