#include "axis_labels.hpp"
#include "gui_app.hpp"
#include "mesh_fixtures.hpp"
#include "scene_renderer.hpp"

#include <fem/core/mesh_builder.hpp>
#include <fem/core/result_sampling.hpp>
#include <fem/geom/commands.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/result_display.hpp>
#include <fem/render/result_scene.hpp>
#include <fem/render/selection.hpp>

#include <QGuiApplication>
#include <QImage>
#include <QSize>

#include <catch2/catch_test_macros.hpp>
#include <rhi/qrhi.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

using fem::render::test::ensureGuiApplication;

} // namespace

// REQ-002-24
TEST_CASE("axis label atlas holds three opaque glyphs", "[render][gizmo]")
{
    ensureGuiApplication();

    constexpr int kCell = 32;
    const QImage atlas = fem::render::buildAxisLabelAtlas(kCell);
    REQUIRE(atlas.size() == QSize(3 * kCell, kCell));

    // Corners stay transparent and every cell has some ink.
    CHECK(qAlpha(atlas.pixel(0, 0)) == 0);
    for (int glyph = 0; glyph < 3; ++glyph) {
        int inked = 0;
        for (int y = 0; y < kCell; ++y) {
            for (int x = 0; x < kCell; ++x) {
                if (qAlpha(atlas.pixel(glyph * kCell + x, y)) > 128) {
                    ++inked;
                }
            }
        }
        CHECK(inked > 10);
    }
}

// REQ-002-29, REQ-002-30: the renderer loads its shaders, builds its pipelines
// and records frames without a GPU or a window.
TEST_CASE("scene renderer draws frames with the Null backend", "[render][renderer]")
{
    ensureGuiApplication();

    QRhiNullInitParams initParams;
    const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &initParams));
    REQUIRE(rhi != nullptr);

    const QSize size(640, 480);
    const std::unique_ptr<QRhiTexture> color(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    REQUIRE(color->create());
    const std::unique_ptr<QRhiRenderBuffer> depth(
        rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    REQUIRE(depth->create());

    const std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(QRhiColorAttachment(color.get()), depth.get())));
    const std::unique_ptr<QRhiRenderPassDescriptor> renderPass(
        target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(renderPass.get());
    REQUIRE(target->create());

    fem::render::Camera camera;
    camera.setViewportSize(size.width(), size.height());

    {
        fem::render::SceneRenderer renderer;
        CHECK_FALSE(renderer.isInitialized());

        const bool initialized = renderer.initialize(rhi.get(), renderPass.get(), 1);
        INFO(renderer.errorString().toStdString());
        REQUIRE(initialized);
        CHECK(renderer.isInitialized());

        struct Variant
        {
            double devicePixelRatio;
            bool showGrid;
            bool showAxes;
        };
        // The second variant forces the label atlas to be rebuilt.
        const std::array<Variant, 4> variants = {{
            {.devicePixelRatio = 1.0, .showGrid = true, .showAxes = true},
            {.devicePixelRatio = 2.0, .showGrid = true, .showAxes = true},
            {.devicePixelRatio = 1.0, .showGrid = false, .showAxes = true},
            {.devicePixelRatio = 1.0, .showGrid = true, .showAxes = false},
        }};
        for (const Variant& variant : variants) {
            QRhiCommandBuffer* commandBuffer = nullptr;
            REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
            CHECK(renderer.render(commandBuffer, target.get(),
                                  {.camera = &camera,
                                   .pixelSize = size,
                                   .devicePixelRatio = variant.devicePixelRatio,
                                   .showGrid = variant.showGrid,
                                   .showAxes = variant.showAxes,
                                   .mesh = {},
                                   .drawing = {},
                                   .symbols = {},
                                   .results = {},
                                   .resultsShown = false}));
            CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
            camera.orbit(40.0, 15.0);
        }

        // REQ-004-09, 11, 14, 16, 26: a mesh with faces, lines, points and a
        // selection, in every display mode and color mode.
        const auto mesh =
            fem::render::test::buildMesh({{0, 0, 0},
                                          {1, 0, 0},
                                          {1, 1, 0},
                                          {0, 1, 0},
                                          {0, 0, 1},
                                          {1, 0, 1},
                                          {1, 1, 1},
                                          {0, 1, 1},
                                          {2, 0, 0},
                                          {3, 0, 0}},
                                         {{fem::core::ElementType::Hex8, {0, 1, 2, 3, 4, 5, 6, 7}},
                                          {fem::core::ElementType::Tri3, {1, 8, 2}},
                                          {fem::core::ElementType::Line2, {8, 9}},
                                          {fem::core::ElementType::Point1, {9}}},
                                         {{"solid", {0}}});
        const auto scene = fem::render::buildMeshScene(mesh);
        fem::render::Selection selection;
        std::uint64_t colorVersion = 0;
        for (const auto mode :
             {fem::render::DisplayMode::ShadedWithEdges, fem::render::DisplayMode::Shaded,
              fem::render::DisplayMode::Wireframe}) {
            // Elements only, then faces, edges and nodes with the nodes shown
            // (REQ-004-26, 34).
            for (const bool mixed : {false, true}) {
                selection.reset(scene.get());
                fem::render::SelectionItems picked;
                if (mixed) {
                    picked[fem::render::SelectionKind::Face] = {0, 1};
                    picked[fem::render::SelectionKind::Edge] = {0, 5};
                    picked[fem::render::SelectionKind::Node] = {0, 1, 2};
                } else {
                    picked[fem::render::SelectionKind::Element] = {0, 1, 2};
                }
                selection.add(picked);
                fem::render::MeshFrame frame;
                frame.scene = scene.get();
                frame.sceneVersion = 1;
                frame.colorVersion = ++colorVersion;
                frame.nodeVersion = colorVersion;
                frame.display.mode = mode;
                frame.display.showNodes = mixed;
                frame.display.color =
                    mixed ? fem::render::ColorMode::ByDimension : fem::render::ColorMode::ByGroup;
                frame.display.dimensionVisible = {true, true,
                                                  mode != fem::render::DisplayMode::Shaded, true};
                frame.selection = &selection;

                QRhiCommandBuffer* commandBuffer = nullptr;
                REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
                const bool rendered = renderer.render(commandBuffer, target.get(),
                                                      {.camera = &camera,
                                                       .pixelSize = size,
                                                       .devicePixelRatio = 1.0,
                                                       .showGrid = true,
                                                       .showAxes = true,
                                                       .mesh = frame,
                                                       .drawing = {},
                                                       .symbols = {},
                                                       .results = {},
                                                       .resultsShown = false});
                INFO(renderer.errorString().toStdString());
                CHECK(rendered);
                CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
            }
        }

        // REQ-005-18, REQ-005-33: a drawing with a line, a polyline and a
        // point, a selected entity and the preview of a running command.
        fem::geom::Drawing drawing;
        const auto line = drawing.nextId();
        const auto polyline = drawing.nextId();
        const auto point = drawing.nextId();
        drawing.apply(
            {.removed = {},
             .added = {
                 {.id = line, .shape = fem::geom::LineEntity{.start = {0, 0, 0}, .end = {4, 0, 0}}},
                 {.id = polyline,
                  .shape = fem::geom::PolylineEntity{.vertices = {{0, 0, 0}, {0, 3, 0}, {4, 3, 2}},
                                                     .closed = true}},
                 {.id = point, .shape = fem::geom::PointEntity{.position = {2, 1, 0}}}}});
        const std::vector<fem::geom::EntityId> drawingSelection = {polyline};
        fem::geom::Preview preview;
        preview.rubberBand = std::make_pair(fem::geom::Vec3(4, 0, 0), fem::geom::Vec3(6, 1, 0));
        preview.ghosts.push_back(
            {.id = fem::geom::EntityId{}, .shape = fem::geom::PointEntity{.position = {5, 5, 0}}});
        for (std::uint64_t version = 1; version <= 2; ++version) {
            fem::render::DrawingFrame drawingFrame;
            drawingFrame.drawing = &drawing;
            drawingFrame.selection = &drawingSelection;
            drawingFrame.selectionVersion = version;
            drawingFrame.preview = &preview;
            drawingFrame.previewVersion = version;
            QRhiCommandBuffer* commandBuffer = nullptr;
            REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
            const bool rendered = renderer.render(commandBuffer, target.get(),
                                                  {.camera = &camera,
                                                   .pixelSize = size,
                                                   .devicePixelRatio = 1.0,
                                                   .showGrid = true,
                                                   .showAxes = true,
                                                   .mesh = {},
                                                   .drawing = drawingFrame,
                                                   .symbols = {},
                                                   .results = {},
                                                   .resultsShown = false});
            INFO(renderer.errorString().toStdString());
            CHECK(rendered);
            CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
        }

        renderer.release();
        CHECK_FALSE(renderer.isInitialized());

        // It can be set up again after a release (e.g. a new QRhi).
        REQUIRE(renderer.initialize(rhi.get(), renderPass.get(), 1));
    }
}

namespace {

// Two bars, (0, 0)-(2, 0) and (2, 0)-(2, 2), three samples each.
std::shared_ptr<const fem::render::ResultScene> twoBarResult()
{
    using Vec = Eigen::Vector3d;
    fem::core::MeshBuilder builder;
    REQUIRE(builder.addNode(1, Vec(0, 0, 0)).has_value());
    REQUIRE(builder.addNode(2, Vec(2, 0, 0)).has_value());
    REQUIRE(builder.addNode(3, Vec(2, 2, 0)).has_value());
    const std::vector<fem::core::ExternalId> a = {1, 2};
    const std::vector<fem::core::ExternalId> b = {2, 3};
    REQUIRE(builder.addElement(fem::core::ElementType::Line2, 1, a).has_value());
    REQUIRE(builder.addElement(fem::core::ElementType::Line2, 2, b).has_value());
    auto built = builder.build();
    REQUIRE(built.has_value());
    const auto mesh = std::make_shared<const fem::core::Mesh>(std::move(*built));
    fem::core::ShapeSamples shape;
    shape.bars = {{.element = 0, .first = 0, .count = 3}, {.element = 1, .first = 3, .count = 3}};
    for (const double x : {0.0, 1.0, 2.0, 0.0, 1.0, 2.0}) {
        shape.stations.push_back({.x = x, .side = fem::core::Side::After, .node = -1});
    }
    shape.positions = {Vec(0, 0, 0), Vec(1, 0, 0), Vec(2, 0, 0),
                       Vec(2, 0, 0), Vec(2, 1, 0), Vec(2, 2, 0)};
    shape.displacements.assign(6, Vec(0.0, -0.01, 0.0));
    shape.bounds = {.min = Vec(0, 0, 0), .max = Vec(2, 2, 0), .empty = false};
    return fem::render::buildResultScene(shape, mesh);
}

} // namespace

// REQ-012-05, 07, 08, 09, 12, 13, NFR-012-03: the result pass records
// frames with the Null backend; the scale, the range and the bands are
// uniforms, so they send nothing; new values send only the values.
TEST_CASE("the result pass draws with the Null backend", "[render][renderer][result]")
{
    ensureGuiApplication();
    QRhiNullInitParams initParams;
    const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &initParams));
    REQUIRE(rhi != nullptr);
    const QSize size(320, 240);
    const std::unique_ptr<QRhiTexture> color(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    REQUIRE(color->create());
    const std::unique_ptr<QRhiRenderBuffer> depth(
        rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    REQUIRE(depth->create());
    const std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(QRhiColorAttachment(color.get()), depth.get())));
    const std::unique_ptr<QRhiRenderPassDescriptor> renderPass(
        target->newCompatibleRenderPassDescriptor());
    target->setRenderPassDescriptor(renderPass.get());
    REQUIRE(target->create());

    fem::render::Camera camera;
    camera.setViewportSize(size.width(), size.height());
    fem::render::SceneRenderer renderer;
    const bool initialized = renderer.initialize(rhi.get(), renderPass.get(), 1);
    INFO(renderer.errorString().toStdString());
    REQUIRE(initialized);

    const auto scene = twoBarResult();
    REQUIRE(scene->segmentStarts().size() == 4);
    const std::vector<float> first = {0, 1, 1, 2, 2, 3, 3, 4};
    const std::vector<float> second = {4, 3, 3, 2, 2, 1, 1, 0};
    fem::render::Selection selection;
    fem::render::SelectionItems items;
    items[fem::render::SelectionKind::Element] = {1};
    items[fem::render::SelectionKind::Node] = {2};
    selection.add(items);

    const auto frame = [&](const fem::render::ResultDisplay& display,
                           const std::vector<float>* values, std::uint64_t valueVersion,
                           bool shown) {
        QRhiCommandBuffer* commandBuffer = nullptr;
        REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
        const bool rendered = renderer.render(commandBuffer, target.get(),
                                              {.camera = &camera,
                                               .pixelSize = size,
                                               .devicePixelRatio = 1.0,
                                               .showGrid = true,
                                               .showAxes = false,
                                               .mesh = {},
                                               .drawing = {},
                                               .symbols = {},
                                               .results = {.scene = scene.get(),
                                                           .sceneVersion = 1,
                                                           .values = values,
                                                           .valueVersion = valueVersion,
                                                           .selection = &selection,
                                                           .selectionVersion = 1,
                                                           .highlight = true,
                                                           .display = display,
                                                           .scale = display.scale},
                                               .resultsShown = shown});
        INFO(renderer.errorString().toStdString());
        CHECK(rendered);
        CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    };

    fem::render::ResultDisplay display;
    display.min = 0.0;
    display.max = 4.0;
    frame(display, &first, 1, true);
    const auto sent = renderer.resultUploads();
    CHECK(sent.geometry == 1);
    CHECK(sent.values == 1);
    CHECK(sent.flags == 1);

    // REQ-012-07, 08, 12: the original under the deformed shape, another
    // scale, bands, a fixed range, no colors: nothing sent again.
    display.original = true;
    display.scale = 50.0;
    frame(display, &first, 1, true);
    display.bands = 5;
    display.min = 1.0;
    display.max = 3.0;
    frame(display, &first, 1, true);
    display.colored = false;
    display.deformed = false;
    frame(display, &first, 1, true);
    CHECK(renderer.resultUploads().geometry == 1);
    CHECK(renderer.resultUploads().values == 1);
    CHECK(renderer.resultUploads().flags == 1);

    // REQ-012-13: another quantity sends the values only.
    display.colored = true;
    frame(display, &second, 2, true);
    CHECK(renderer.resultUploads().geometry == 1);
    CHECK(renderer.resultUploads().values == 2);
    CHECK(renderer.resultUploads().flags == 1);

    // Hidden: the mesh pass instead, and the result kept for later.
    frame(display, &second, 2, false);
    frame(display, &second, 2, true);
    CHECK(renderer.resultUploads().geometry == 1);
    CHECK(renderer.resultUploads().values == 2);
    renderer.release();
}
