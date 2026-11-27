// Feature 012 (design Â§5.5): the scene of a result, ready for the GPU.

#include <fem/core/mesh_builder.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/result_display.hpp>
#include <fem/render/result_picker.hpp>
#include <fem/render/result_scene.hpp>

#include <catch2/catch_test_macros.hpp>

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

using fem::core::Side;
using fem::core::Station;
using Vec = Eigen::Vector3d;

namespace {

// Two bars: 1 from (0, 0) to (2, 0), 2 from (2, 0) to (2, 2).
std::shared_ptr<const fem::core::Mesh> twoBars()
{
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(2, Vec(2, 0, 0)).has_value());
    REQUIRE(builder.addNode(3, Vec(2, 2, 0)).has_value());
    const std::vector<fem::core::ExternalId> a = {1, 2};
    const std::vector<fem::core::ExternalId> b = {2, 3};
    REQUIRE(builder.addElement(fem::core::ElementType::Line2, 1, a).has_value());
    REQUIRE(builder.addElement(fem::core::ElementType::Line2, 2, b).has_value());
    auto mesh = builder.build();
    REQUIRE(mesh.has_value());
    return std::make_shared<const fem::core::Mesh>(std::move(*mesh));
}

Station at(double x, Side side = Side::After, fem::core::NodeIndex node = -1)
{
    return Station{.x = x, .side = side, .node = node};
}

// Bar 1: 0, 1 (both sides of a load), 2; bar 2: its start node (a rigid
// link), then 0 and 2 along the flexible length.
fem::core::ShapeSamples samples()
{
    fem::core::ShapeSamples shape;
    shape.bars = {{.element = 0, .first = 0, .count = 4}, {.element = 1, .first = 4, .count = 3}};
    shape.stations = {at(0.0), at(1.0, Side::Before),   at(1.0),
                      at(2.0), at(0.0, Side::After, 1), at(0.0),
                      at(2.0)};
    shape.positions = {Vec(0, 0, 0), Vec(1, 0, 0),   Vec(1, 0, 0), Vec(2, 0, 0),
                       Vec(2, 0, 0), Vec(2, 0.1, 0), Vec(2, 2, 0)};
    for (std::size_t i = 0; i < shape.positions.size(); ++i) {
        shape.displacements.emplace_back(0.0, -0.01 * static_cast<double>(i), 0.0);
    }
    shape.bounds = {.min = Vec(0, 0, 0), .max = Vec(2, 2, 0), .empty = false};
    return shape;
}

} // namespace

// REQ-012-05, 10, NFR-012-04: segments between the samples of each bar.
TEST_CASE("the scene of a result", "[render][result]")
{
    const auto mesh = twoBars();
    const auto scene = fem::render::buildResultScene(samples(), mesh);
    REQUIRE(scene);
    CHECK(scene->mesh() == mesh);
    // The centre of the bars, and the positions from it.
    CHECK((scene->origin() - Vec(1, 1, 0)).norm() == 0.0);
    REQUIRE(scene->positions().size() == 7);
    CHECK((scene->positions()[0] - Eigen::Vector3f(-1, -1, 0)).norm() == 0.0F);
    CHECK((scene->positions()[6] - Eigen::Vector3f(1, 1, 0)).norm() == 0.0F);
    CHECK(std::abs(scene->displacements()[3].y() + 0.03F) <= 1e-7F);
    CHECK(scene->stations().size() == 7);
    CHECK(scene->bars().size() == 2);
    // The two sides of the load make no segment; the rigid link does.
    CHECK(
        std::vector<std::uint32_t>(scene->segmentStarts().begin(), scene->segmentStarts().end()) ==
        std::vector<std::uint32_t>{0, 2, 4, 5});
    CHECK(std::vector<fem::core::ElementIndex>(scene->segmentElements().begin(),
                                               scene->segmentElements().end()) ==
          std::vector<fem::core::ElementIndex>{0, 0, 1, 1});
    // The first sample at each node.
    CHECK(std::vector<std::uint32_t>(scene->nodeSamples().begin(), scene->nodeSamples().end()) ==
          std::vector<std::uint32_t>{0, 3, 6});
    // NFR-012-04: at most 64 bytes per sample.
    CHECK(scene->gpuBytes() == 4 * fem::render::kSegmentGpuBytes);
    CHECK(scene->gpuBytes() <= 64 * scene->positions().size());
}

// REQ-012-13: two values per segment, each that of its end.
TEST_CASE("the values of a quantity on the segments", "[render][result]")
{
    const auto scene = fem::render::buildResultScene(samples(), twoBars());
    fem::core::FieldSamples field;
    field.values = {10, 11, 12, 13, 14, 15, std::nan("")};
    const auto values = fem::render::segmentValues(*scene, field);
    REQUIRE(values);
    REQUIRE(values->size() == 8);
    CHECK((*values)[0] == 10.0F);
    CHECK((*values)[1] == 11.0F);
    // After the load: its After side.
    CHECK((*values)[2] == 12.0F);
    CHECK((*values)[3] == 13.0F);
    CHECK((*values)[4] == 14.0F);
    CHECK((*values)[5] == 15.0F);
    CHECK((*values)[6] == 15.0F);
    CHECK(std::isnan((*values)[7]));
}

// An empty result: nothing to draw.
TEST_CASE("the scene of an empty result", "[render][result]")
{
    const auto scene = fem::render::buildResultScene(fem::core::ShapeSamples{}, nullptr);
    REQUIRE(scene);
    CHECK(scene->positions().empty());
    CHECK(scene->segmentStarts().empty());
    CHECK(scene->nodeSamples().empty());
    CHECK(scene->gpuBytes() == 0);
}

namespace {

constexpr int kWidth = 800;
constexpr int kHeight = 600;

// Looking down on the XY plane, the bars filling the view.
fem::render::Camera topCamera()
{
    fem::render::Camera camera;
    camera.setViewportSize(kWidth, kHeight);
    camera.setProjection(fem::render::Projection::Orthographic);
    camera.setStandardView(fem::render::StandardView::Top);
    camera.fit({.min = Vec(-0.5, -0.5, 0), .max = Vec(2.5, 2.5, 0)});
    return camera;
}

fem::render::Vec2 toPixel(const fem::render::Camera& camera, const Vec& world)
{
    const Eigen::Vector4d clip =
        camera.projectionMatrix() * camera.viewMatrix() * world.homogeneous();
    return {(clip.x() / clip.w() + 1.0) * 0.5 * kWidth,
            (1.0 - clip.y() / clip.w()) * 0.5 * kHeight};
}

} // namespace

// REQ-012-18: a click on the shape drawn gives the bar and the point of it,
// or the node.
TEST_CASE("a click on the shape of a result", "[render][result]")
{
    const auto scene = fem::render::buildResultScene(samples(), twoBars());
    const fem::render::Camera camera = topCamera();
    SECTION("the original shape: the bar and x along the segment")
    {
        const auto pick =
            fem::render::pickResult(*scene, 0.0, camera, toPixel(camera, Vec(1.5, 0.0, 0.0)));
        REQUIRE(pick.has_value());
        const fem::render::ResultPick hit = pick.value_or(fem::render::ResultPick{});
        CHECK(hit.element == 0);
        CHECK(hit.node == -1);
        CHECK(std::abs(hit.station.x - 1.5) <= 1e-6 * 1.5);
        CHECK(hit.station.node == -1);
    }
    SECTION("the deformed shape: where it is drawn, not where the bar was")
    {
        // Samples 2 and 3 moved by 10 x (0, -0.02) and 10 x (0, -0.03).
        const auto pick =
            fem::render::pickResult(*scene, 10.0, camera, toPixel(camera, Vec(1.5, -0.25, 0.0)));
        REQUIRE(pick.has_value());
        const fem::render::ResultPick hit = pick.value_or(fem::render::ResultPick{});
        CHECK(hit.element == 0);
        CHECK(std::abs(hit.station.x - 1.5) <= 1e-6 * 1.5);
        CHECK_FALSE(
            fem::render::pickResult(*scene, 10.0, camera, toPixel(camera, Vec(1.5, 0.0, 0.0)))
                .has_value());
    }
    SECTION("a node within the tolerance wins over the segments")
    {
        const auto pick = fem::render::pickResult(
            *scene, 0.0, camera, toPixel(camera, Vec(2.0, 0.0, 0.0)) + fem::render::Vec2(3, 2));
        REQUIRE(pick.has_value());
        const fem::render::ResultPick hit = pick.value_or(fem::render::ResultPick{});
        CHECK(hit.node == 1);
        CHECK(hit.element == 0);
        CHECK(hit.station.x == 2.0);
    }
    SECTION("the second bar, through its rigid link")
    {
        const auto pick =
            fem::render::pickResult(*scene, 0.0, camera, toPixel(camera, Vec(2.0, 1.0, 0.0)));
        REQUIRE(pick.has_value());
        const fem::render::ResultPick hit = pick.value_or(fem::render::ResultPick{});
        CHECK(hit.element == 1);
        CHECK(hit.node == -1);
        // From 0 at (2, 0.1) to 2 at (2, 2).
        CHECK(std::abs(hit.station.x - 2.0 * 0.9 / 1.9) <= 1e-6);
    }
    SECTION("far from everything: nothing")
    {
        CHECK_FALSE(
            fem::render::pickResult(*scene, 0.0, camera, toPixel(camera, Vec(0.5, 1.5, 0.0)))
                .has_value());
    }
}

// REQ-012-15: the factor over time: 0 to f for a static result, -f to f
// for a mode; the factor itself when the animation does not play.
TEST_CASE("the animated scale factor", "[render][result]")
{
    using fem::render::Animation;
    fem::render::ResultDisplay display;
    display.scale = 40.0;
    display.period = 2.0;
    display.animation = Animation::Static;
    display.playing = false;
    CHECK(fem::render::animatedScale(display, 0.7) == 40.0);
    display.playing = true;
    CHECK(std::abs(fem::render::animatedScale(display, 0.0)) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 0.5) - 20.0) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 1.0) - 40.0) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 2.0)) <= 1e-12);
    display.animation = Animation::Mode;
    CHECK(std::abs(fem::render::animatedScale(display, 0.0)) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 0.5) - 40.0) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 1.0)) <= 1e-12);
    CHECK(std::abs(fem::render::animatedScale(display, 1.5) + 40.0) <= 1e-12);
    display.animation = Animation::Off;
    CHECK(fem::render::animatedScale(display, 1.5) == 40.0);
    display.animation = Animation::Mode;
    display.period = 0.0;
    CHECK(fem::render::animatedScale(display, 1.5) == 40.0);
}
