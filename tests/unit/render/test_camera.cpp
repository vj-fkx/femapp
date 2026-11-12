#include <fem/render/camera.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using fem::render::Aabb;
using fem::render::Camera;
using fem::render::Mat4;
using fem::render::Projection;
using fem::render::StandardView;
using fem::render::Vec2;
using fem::render::Vec3;

namespace {

constexpr double kTight = 1e-12;
constexpr double kPixelTolerance = 1e-6;
constexpr int kWidth = 1280;
constexpr int kHeight = 720;

Camera makeCamera(Projection projection = Projection::Perspective)
{
    Camera camera;
    camera.setViewportSize(kWidth, kHeight);
    camera.setProjection(projection);
    return camera;
}

// Normalized device coordinates of a world point: x, y in [-1, 1], depth in [0, 1].
Vec3 toNdc(const Camera& camera, const Vec3& world)
{
    const Eigen::Vector4d clip =
        camera.projectionMatrix() * camera.viewMatrix() * world.homogeneous();
    return clip.head<3>() / clip.w();
}

Vec2 toPixel(const Camera& camera, const Vec3& world)
{
    const Vec3 ndc = toNdc(camera, world);
    return {(ndc.x() + 1.0) * 0.5 * camera.viewportWidth(),
            (1.0 - ndc.y()) * 0.5 * camera.viewportHeight()};
}

void checkVec(const Vec3& actual, const Vec3& expected, double tolerance = kTight)
{
    CHECK_THAT(actual.x(), WithinAbs(expected.x(), tolerance));
    CHECK_THAT(actual.y(), WithinAbs(expected.y(), tolerance));
    CHECK_THAT(actual.z(), WithinAbs(expected.z(), tolerance));
}

void checkOrthonormalBasis(const Camera& camera)
{
    const Vec3 r = camera.right();
    const Vec3 u = camera.up();
    const Vec3 f = camera.forward();
    CHECK_THAT(r.norm(), WithinAbs(1.0, kTight));
    CHECK_THAT(u.norm(), WithinAbs(1.0, kTight));
    CHECK_THAT(f.norm(), WithinAbs(1.0, kTight));
    CHECK_THAT(r.dot(u), WithinAbs(0.0, kTight));
    CHECK_THAT(r.dot(f), WithinAbs(0.0, kTight));
    CHECK_THAT(u.dot(f), WithinAbs(0.0, kTight));
    // Right-handed: right x up points towards the viewer.
    checkVec(r.cross(u), -f);
}

} // namespace

// REQ-002-19
TEST_CASE("camera starts in the isometric perspective view", "[render][camera]")
{
    const Camera camera;
    CHECK(camera.projection() == Projection::Perspective);

    const double s = 1.0 / std::numbers::sqrt3;
    checkVec(camera.forward(), Vec3(-s, s, -s));
    checkVec(camera.target(), Vec3::Zero());
}

// REQ-002-11, REQ-002-13, REQ-002-28
TEST_CASE("orbiting keeps an orthonormal basis with Z up", "[render][camera]")
{
    Camera camera = makeCamera();
    std::mt19937 generator(12345);
    std::uniform_real_distribution<double> delta(-400.0, 400.0);

    for (int i = 0; i < 500; ++i) {
        camera.orbit(delta(generator), delta(generator));

        checkOrthonormalBasis(camera);
        // The horizon stays level and world Z never points down on screen.
        CHECK_THAT(camera.right().z(), WithinAbs(0.0, kTight));
        CHECK(camera.up().z() >= -kTight);
        CHECK(std::abs(camera.pitch()) <= std::numbers::pi / 2.0);
    }
}

// REQ-002-13
TEST_CASE("orbiting stops at the poles with a defined basis", "[render][camera]")
{
    Camera camera = makeCamera();
    camera.setStandardView(StandardView::Front);

    camera.orbit(0.0, 1e6);
    CHECK_THAT(camera.pitch(), WithinAbs(std::numbers::pi / 2.0, kTight));
    checkOrthonormalBasis(camera);
    checkVec(camera.forward(), Vec3(0.0, 0.0, -1.0));

    camera.orbit(0.0, -1e7);
    CHECK_THAT(camera.pitch(), WithinAbs(-std::numbers::pi / 2.0, kTight));
    checkOrthonormalBasis(camera);
    checkVec(camera.forward(), Vec3(0.0, 0.0, 1.0));
}

// REQ-002-13
TEST_CASE("orbiting does not move the target or change the distance", "[render][camera]")
{
    Camera camera = makeCamera();
    const Vec3 target = camera.target();
    const double distance = camera.distance();

    camera.orbit(123.0, -45.0);

    checkVec(camera.target(), target);
    CHECK(camera.distance() == distance);
    CHECK_THAT((camera.position() - target).norm(), WithinRel(distance, kTight));
}

// REQ-002-17
TEST_CASE("standard views look along the expected axes", "[render][camera]")
{
    struct Expected
    {
        StandardView view;
        Vec3 forward;
        Vec3 right;
        Vec3 up;
    };
    const std::array<Expected, 6> cases = {{
        {.view = StandardView::Front,
         .forward = Vec3(0, 1, 0),
         .right = Vec3(1, 0, 0),
         .up = Vec3(0, 0, 1)},
        {.view = StandardView::Back,
         .forward = Vec3(0, -1, 0),
         .right = Vec3(-1, 0, 0),
         .up = Vec3(0, 0, 1)},
        {.view = StandardView::Right,
         .forward = Vec3(-1, 0, 0),
         .right = Vec3(0, 1, 0),
         .up = Vec3(0, 0, 1)},
        {.view = StandardView::Left,
         .forward = Vec3(1, 0, 0),
         .right = Vec3(0, -1, 0),
         .up = Vec3(0, 0, 1)},
        {.view = StandardView::Top,
         .forward = Vec3(0, 0, -1),
         .right = Vec3(1, 0, 0),
         .up = Vec3(0, 1, 0)},
        {.view = StandardView::Bottom,
         .forward = Vec3(0, 0, 1),
         .right = Vec3(1, 0, 0),
         .up = Vec3(0, -1, 0)},
    }};

    Camera camera = makeCamera();
    for (const Expected& expected : cases) {
        camera.setStandardView(expected.view);
        checkVec(camera.forward(), expected.forward);
        checkVec(camera.right(), expected.right);
        checkVec(camera.up(), expected.up);
        checkOrthonormalBasis(camera);
    }

    camera.setStandardView(StandardView::Isometric);
    const double s = 1.0 / std::numbers::sqrt3;
    checkVec(camera.forward(), Vec3(-s, s, -s));
    CHECK(camera.up().z() > 0.0);
}

// REQ-002-28
TEST_CASE("view matrix maps the target in front of the eye", "[render][camera]")
{
    Camera camera = makeCamera();
    camera.orbit(77.0, 31.0);
    camera.pan(Vec2(100, 100), Vec2(350, 220));

    const Mat4 view = camera.viewMatrix();
    const Eigen::Vector4d targetInView = view * camera.target().homogeneous();
    checkVec(targetInView.head<3>(), Vec3(0.0, 0.0, -camera.distance()), 1e-9);

    const Eigen::Vector4d eyeInView = view * camera.position().homogeneous();
    checkVec(eyeInView.head<3>(), Vec3::Zero(), 1e-9);

    // The target-relative matrix is the same transform with the target at 0.
    const Eigen::Vector4d relative =
        camera.viewMatrixRelativeToTarget() * Eigen::Vector4d(0.0, 0.0, 0.0, 1.0);
    checkVec(relative.head<3>(), targetInView.head<3>(), 1e-9);
}

// REQ-002-16, REQ-002-28
TEST_CASE("projection maps the target to the centre with depth in [0, 1]", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = makeCamera(projection);
    camera.orbit(-60.0, 20.0);

    const Vec3 ndc = toNdc(camera, camera.target());
    CHECK_THAT(ndc.x(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(ndc.y(), WithinAbs(0.0, 1e-12));
    CHECK(ndc.z() > 0.0);
    CHECK(ndc.z() < 1.0);

    // A point above the target on screen has a positive clip-space Y.
    CHECK(toNdc(camera, camera.target() + camera.up()).y() > 0.0);
    CHECK(toNdc(camera, camera.target() + camera.right()).x() > 0.0);
}

// REQ-002-16, REQ-002-28
TEST_CASE("rays agree with the view and projection matrices", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = makeCamera(projection);
    camera.orbit(140.0, -35.0);

    for (const Vec2& pixel : {Vec2(0, 0), Vec2(640, 360), Vec2(1279, 719), Vec2(200, 650)}) {
        const fem::render::Ray ray = camera.rayThrough(pixel);
        CHECK_THAT(ray.direction.norm(), WithinAbs(1.0, kTight));

        // Any point along the ray projects back to the same pixel.
        for (const double t : {0.5 * camera.distance(), 3.0 * camera.distance()}) {
            const Vec2 projected = toPixel(camera, ray.origin + t * ray.direction);
            CHECK_THAT(projected.x(), WithinAbs(pixel.x(), kPixelTolerance));
            CHECK_THAT(projected.y(), WithinAbs(pixel.y(), kPixelTolerance));
        }

        const Vec3 onPlane = camera.focalPlanePoint(pixel);
        CHECK_THAT((onPlane - camera.target()).dot(camera.forward()), WithinAbs(0.0, 1e-9));
    }
}

// REQ-002-16
TEST_CASE("switching projection keeps the focus and the apparent size", "[render][camera]")
{
    Camera camera = makeCamera(Projection::Perspective);
    camera.orbit(30.0, 10.0);
    const Vec3 target = camera.target();
    const Vec3 probe = target + 2.5 * camera.up() + 1.5 * camera.right();
    const Vec2 before = toPixel(camera, probe);
    const double unitsPerPixel = camera.worldUnitsPerPixel();

    camera.setProjection(Projection::Orthographic);

    checkVec(camera.target(), target);
    const Vec2 after = toPixel(camera, probe);
    CHECK_THAT(after.x(), WithinAbs(before.x(), kPixelTolerance));
    CHECK_THAT(after.y(), WithinAbs(before.y(), kPixelTolerance));
    CHECK_THAT(camera.worldUnitsPerPixel(), WithinRel(unitsPerPixel, kTight));
}

// REQ-002-08
TEST_CASE("a degenerate viewport size is ignored", "[render][camera]")
{
    Camera camera = makeCamera();
    camera.setViewportSize(0, 500);
    camera.setViewportSize(500, -1);
    CHECK(camera.viewportWidth() == kWidth);
    CHECK(camera.viewportHeight() == kHeight);
}

// REQ-002-12, REQ-002-28
TEST_CASE("panning keeps the grabbed point under the cursor", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = makeCamera(projection);
    camera.orbit(50.0, 25.0);

    const Vec2 from(300.0, 200.0);
    const Vec2 to(912.0, 541.0);
    REQUIRE(camera.workPlanePoint(from).has_value());
    REQUIRE(camera.workPlanePoint(to).has_value());
    const Vec3 grabbed = camera.anchorPoint(from);
    const Vec3 forward = camera.forward();
    const double distance = camera.distance();
    const double height = camera.target().z();

    camera.pan(from, to);

    const Vec2 projected = toPixel(camera, grabbed);
    CHECK_THAT(projected.x(), WithinAbs(to.x(), kPixelTolerance));
    CHECK_THAT(projected.y(), WithinAbs(to.y(), kPixelTolerance));
    // Panning on the work plane neither rotates, zooms nor changes the height.
    checkVec(camera.forward(), forward);
    CHECK(camera.distance() == distance);
    CHECK_THAT(camera.target().z(), WithinAbs(height, 1e-12));
}

// REQ-002-14, REQ-002-28
TEST_CASE("zooming keeps the point under the cursor fixed", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const double steps = GENERATE(1.0, -1.0, 7.5, -12.0);
    Camera camera = makeCamera(projection);
    camera.orbit(-20.0, 40.0);

    const Vec2 cursor(1000.0, 150.0);
    const Vec3 anchor = camera.anchorPoint(cursor);
    const double before = camera.distance();

    camera.zoomAt(cursor, steps);

    const Vec2 projected = toPixel(camera, anchor);
    CHECK_THAT(projected.x(), WithinAbs(cursor.x(), kPixelTolerance));
    CHECK_THAT(projected.y(), WithinAbs(cursor.y(), kPixelTolerance));
    if (steps > 0.0) {
        CHECK(camera.distance() < before);
    } else {
        CHECK(camera.distance() > before);
    }
}

// REQ-002-12: above the horizon the work plane is out of reach; panning falls
// back to the focal plane.
TEST_CASE("panning above the horizon uses the focal plane", "[render][camera]")
{
    Camera camera = makeCamera();
    camera.setStandardView(StandardView::Front);

    const Vec2 from(400.0, 100.0);
    const Vec2 to(700.0, 200.0);
    REQUIRE_FALSE(camera.workPlanePoint(from).has_value());
    const Vec3 grabbed = camera.focalPlanePoint(from);

    camera.pan(from, to);

    const Vec2 projected = toPixel(camera, grabbed);
    CHECK_THAT(projected.x(), WithinAbs(to.x(), kPixelTolerance));
    CHECK_THAT(projected.y(), WithinAbs(to.y(), kPixelTolerance));
}

// REQ-002-14, REQ-002-20: zooming in on the grid away from the centre of the
// screen keeps the target on the grid, so the grid stays in view.
TEST_CASE("zooming in on the grid keeps the target on the work plane", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = makeCamera(projection);

    const Vec2 cursor(1000.0, 600.0);
    REQUIRE(camera.workPlanePoint(cursor).has_value());
    const Vec3 anchor = camera.anchorPoint(cursor);
    CHECK_THAT(anchor.z(), WithinAbs(0.0, 1e-9));

    for (int step = 0; step < 300; ++step) {
        camera.zoomAt(cursor, 1.0);
    }

    CHECK(camera.distance() < 1e-3);
    CHECK_THAT(camera.target().z(), WithinAbs(0.0, 1e-9));
    const Vec2 projected = toPixel(camera, anchor);
    CHECK_THAT(projected.x(), WithinAbs(cursor.x(), 1e-3));
    CHECK_THAT(projected.y(), WithinAbs(cursor.y(), 1e-3));
    // The grid under the cursor is inside the depth range.
    const Vec3 ndc = toNdc(camera, camera.anchorPoint(cursor));
    CHECK(ndc.z() > 0.0);
    CHECK(ndc.z() < 1.0);
}

// REQ-002-14: pointing at the sky, zoom anchors on the focal plane.
TEST_CASE("zooming above the horizon anchors on the focal plane", "[render][camera]")
{
    Camera camera = makeCamera();
    camera.setStandardView(StandardView::Front);

    const Vec2 cursor(900.0, 80.0);
    REQUIRE_FALSE(camera.workPlanePoint(cursor).has_value());
    const Vec3 anchor = camera.focalPlanePoint(cursor);
    CHECK_THAT((camera.anchorPoint(cursor) - anchor).norm(), WithinAbs(0.0, 1e-12));

    camera.zoomAt(cursor, 3.0);

    const Vec2 projected = toPixel(camera, anchor);
    CHECK_THAT(projected.x(), WithinAbs(cursor.x(), kPixelTolerance));
    CHECK_THAT(projected.y(), WithinAbs(cursor.y(), kPixelTolerance));
}

// REQ-002-15
TEST_CASE("zoom is clamped and stops moving the target at the limits", "[render][camera]")
{
    Camera camera = makeCamera();
    const Vec2 cursor(100.0, 100.0);

    camera.zoomAt(cursor, 1e4);
    CHECK(camera.distance() == Camera::kMinDistance);
    const Vec3 targetAtMin = camera.target();
    camera.zoomAt(cursor, 10.0);
    CHECK(camera.distance() == Camera::kMinDistance);
    checkVec(camera.target(), targetAtMin);

    camera.zoomAt(cursor, -1e4);
    CHECK(camera.distance() == Camera::kMaxDistance);
    const Vec3 targetAtMax = camera.target();
    camera.zoomAt(cursor, -10.0);
    CHECK(camera.distance() == Camera::kMaxDistance);
    checkVec(camera.target(), targetAtMax);
    CHECK(camera.target().allFinite());
}

// REQ-002-18, REQ-002-28
TEST_CASE("fit frames a box tightly in every view", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    const StandardView view = GENERATE(StandardView::Front, StandardView::Top, StandardView::Right,
                                       StandardView::Isometric);
    const auto [width, height] = GENERATE(std::pair{1600, 600}, std::pair{600, 1600});

    Camera camera;
    camera.setViewportSize(width, height);
    camera.setProjection(projection);
    camera.setStandardView(view);

    const Aabb box{.min = Vec3(100.0, -20.0, 5.0), .max = Vec3(130.0, 40.0, 17.0)};
    camera.fit(box);

    checkVec(camera.target(), Vec3(115.0, 10.0, 11.0), 1e-9);

    double largest = 0.0;
    for (const double x : {box.min.x(), box.max.x()}) {
        for (const double y : {box.min.y(), box.max.y()}) {
            for (const double z : {box.min.z(), box.max.z()}) {
                const Vec3 ndc = toNdc(camera, Vec3(x, y, z));
                largest = std::max({largest, std::abs(ndc.x()), std::abs(ndc.y())});
                CHECK(ndc.z() > 0.0);
                CHECK(ndc.z() < 1.0);
            }
        }
    }
    // Everything is inside the frame and the slack is at most 25 %.
    CHECK(largest <= 1.0);
    CHECK(largest >= 0.75);
}

// REQ-002-18, REQ-002-19
TEST_CASE("fit falls back to the default region for an unusable box", "[render][camera]")
{
    Camera reference = makeCamera();
    reference.fit(fem::render::defaultSceneBounds());

    Camera camera = makeCamera();
    camera.zoomAt(Vec2(10, 10), 5.0);

    SECTION("empty box")
    {
        camera.fit(Aabb{});
    }
    SECTION("inverted box")
    {
        camera.fit(Aabb{.min = Vec3(1, 1, 1), .max = Vec3(0, 0, 0)});
    }
    SECTION("non-finite box")
    {
        camera.fit(Aabb{.min = Vec3(0, 0, 0), .max = Vec3(std::nan(""), 1, 1)});
    }

    checkVec(camera.target(), reference.target());
    CHECK_THAT(camera.distance(), WithinRel(reference.distance(), kTight));
}

// NFR-002-03: with the target far from the origin, single precision is enough
// for the GPU as long as positions are sent relative to the target.
TEST_CASE("target-relative matrices survive the conversion to float", "[render][camera]")
{
    Camera camera = makeCamera();
    const Vec3 farAway(1.0e6, 1.0e6, 0.0);
    camera.fit(Aabb{.min = farAway - Vec3(1, 1, 0), .max = farAway + Vec3(1, 1, 0)});

    const Eigen::Matrix4f viewProjection =
        (camera.projectionMatrix() * camera.viewMatrixRelativeToTarget()).cast<float>();

    for (const Vec3& offset : {Vec3(0.3, 0.2, 0.1), Vec3(-0.9, 0.7, 0.0), Vec3(0.0, 0.0, 0.5)}) {
        const Vec3 world = farAway + offset;
        const Vec2 expected = toPixel(camera, world);

        // The subtraction happens in double; only the small offset goes to float.
        const Eigen::Vector3f relative = (world - camera.target()).cast<float>();
        const Eigen::Vector4f clip = viewProjection * relative.homogeneous();
        const double x = (clip.x() / clip.w() + 1.0) * 0.5 * kWidth;
        const double y = (1.0 - clip.y() / clip.w()) * 0.5 * kHeight;

        CHECK_THAT(x, WithinAbs(expected.x(), 1e-3));
        CHECK_THAT(y, WithinAbs(expected.y(), 1e-3));
    }
}

// REQ-002-13 (changed in feature 004): moving the pivot keeps the image.
TEST_CASE("setting the orbit pivot does not change the image", "[render][camera]")
{
    const Projection projection = GENERATE(Projection::Perspective, Projection::Orthographic);
    Camera camera = makeCamera(projection);
    camera.orbit(35.0, -20.0);
    camera.pan(Vec2(200, 200), Vec2(450, 260));

    const std::array<Vec3, 3> probes = {Vec3(0.3, -1.2, 2.0), Vec3(4.0, 1.0, -0.5),
                                        Vec3(-2.0, 3.0, 1.0)};
    std::array<Vec2, 3> before{};
    for (std::size_t i = 0; i < probes.size(); ++i) {
        before.at(i) = toPixel(camera, probes.at(i));
    }

    // A point on the central line of sight, nearer than the current target.
    const Vec3 pivot = camera.position() + 0.4 * camera.distance() * camera.forward();
    camera.setPivot(pivot);

    checkVec(camera.target(), pivot, 1e-9);
    for (std::size_t i = 0; i < probes.size(); ++i) {
        const Vec2 after = toPixel(camera, probes.at(i));
        CHECK_THAT(after.x(), WithinAbs(before.at(i).x(), kPixelTolerance));
        CHECK_THAT(after.y(), WithinAbs(before.at(i).y(), kPixelTolerance));
    }

    // Orbiting now keeps the pivot at the centre of the screen.
    camera.orbit(60.0, 25.0);
    const Vec2 centre = toPixel(camera, pivot);
    CHECK_THAT(centre.x(), WithinAbs(kWidth / 2.0, kPixelTolerance));
    CHECK_THAT(centre.y(), WithinAbs(kHeight / 2.0, kPixelTolerance));
}

// REQ-002-13: a pivot behind the eye is ignored in perspective.
TEST_CASE("a pivot behind the eye is ignored", "[render][camera]")
{
    Camera camera = makeCamera(Projection::Perspective);
    const Vec3 target = camera.target();
    camera.setPivot(camera.position() - camera.forward());
    checkVec(camera.target(), target);
}
