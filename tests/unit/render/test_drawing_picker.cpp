#include <fem/geom/drawing.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/drawing_picker.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <optional>
#include <vector>

using fem::geom::Drawing;
using fem::geom::EntityId;
using fem::geom::Vec3;
using fem::render::Camera;
using fem::render::DrawingPicker;
using fem::render::PickRect;
using fem::render::Vec2;

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr double kTolerance = 5.0;

// A plane frame seen from above: two columns, a beam and a support point.
struct Frame
{
    Drawing drawing;
    EntityId left{};
    EntityId right{};
    EntityId beam{};
    EntityId support{};
};

void build(Frame& frame)
{
    Drawing& d = frame.drawing;
    frame.left = d.nextId();
    frame.right = d.nextId();
    frame.beam = d.nextId();
    frame.support = d.nextId();
    d.apply({.removed = {},
             .added = {
                 {.id = frame.left,
                  .shape = fem::geom::LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(0, 3, 0)}},
                 {.id = frame.right,
                  .shape = fem::geom::LineEntity{.start = Vec3(4, 0, 0), .end = Vec3(4, 3, 0)}},
                 {.id = frame.beam,
                  .shape = fem::geom::PolylineEntity{.vertices = {Vec3(0, 3, 0), Vec3(2, 4, 0),
                                                                  Vec3(4, 3, 0)},
                                                     .closed = false}},
                 {.id = frame.support, .shape = fem::geom::PointEntity{.position = Vec3(2, -1, 0)}},
             }});
}

Camera topCamera(const Drawing& drawing)
{
    Camera camera;
    camera.setViewportSize(kWidth, kHeight);
    camera.setProjection(fem::render::Projection::Orthographic);
    camera.setStandardView(fem::render::StandardView::Top);
    const auto box = drawing.bounds().value_or(fem::geom::Box{});
    camera.fit({.min = box.min, .max = box.max});
    return camera;
}

Vec2 toPixel(const Camera& camera, const Vec3& world)
{
    const Eigen::Vector4d clip =
        camera.projectionMatrix() * camera.viewMatrix() * world.homogeneous();
    return {(clip.x() / clip.w() + 1.0) * 0.5 * kWidth,
            (1.0 - clip.y() / clip.w()) * 0.5 * kHeight};
}

std::vector<EntityId> sorted(std::vector<EntityId> ids)
{
    std::ranges::sort(ids);
    return ids;
}

} // namespace

// REQ-005-25: clicks on lines, polyline pieces and points.
TEST_CASE("a click picks drawing entities within the tolerance", "[render][drawing]")
{
    Frame frame;
    build(frame);
    const Camera camera = topCamera(frame.drawing);
    const DrawingPicker picker;

    const Vec2 onLeft = toPixel(camera, Vec3(0, 1.5, 0));
    CHECK(picker.pick(frame.drawing, camera, onLeft + Vec2(3, 0), kTolerance) ==
          std::optional<EntityId>(frame.left));
    CHECK_FALSE(picker.pick(frame.drawing, camera, onLeft + Vec2(20, 0), kTolerance).has_value());

    const Vec2 onBeam = toPixel(camera, Vec3(3, 3.5, 0));
    CHECK(picker.pick(frame.drawing, camera, onBeam, kTolerance) ==
          std::optional<EntityId>(frame.beam));

    const Vec2 onSupport = toPixel(camera, Vec3(2, -1, 0));
    CHECK(picker.pick(frame.drawing, camera, onSupport + Vec2(2, 2), kTolerance) ==
          std::optional<EntityId>(frame.support));
}

// REQ-005-25: window and crossing.
TEST_CASE("window and crossing select drawing entities", "[render][drawing]")
{
    Frame frame;
    build(frame);
    const Camera camera = topCamera(frame.drawing);
    const DrawingPicker picker;

    // A rectangle around the left column and the support point, crossing the
    // beam.
    const Vec2 a = toPixel(camera, Vec3(-0.5, -1.5, 0));
    const Vec2 b = toPixel(camera, Vec3(2.5, 3.5, 0));
    const Vec2 low = a.cwiseMin(b);
    const Vec2 high = a.cwiseMax(b);

    const PickRect window{.from = low, .to = high};
    CHECK(sorted(picker.pickRect(frame.drawing, camera, window)) ==
          sorted({frame.left, frame.support}));

    const PickRect crossing{.from = Vec2(high.x(), low.y()), .to = Vec2(low.x(), high.y())};
    CHECK(sorted(picker.pickRect(frame.drawing, camera, crossing)) ==
          sorted({frame.left, frame.beam, frame.support}));
}
