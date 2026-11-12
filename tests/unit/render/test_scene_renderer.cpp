#include "axis_labels.hpp"
#include "mesh_fixtures.hpp"
#include "scene_renderer.hpp"

#include <fem/render/camera.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <QGuiApplication>
#include <QImage>
#include <QSize>

#include <catch2/catch_test_macros.hpp>
#include <rhi/qrhi.h>

#include <array>
#include <memory>

namespace {

// QPainter text (used for the axis labels) needs a QGuiApplication. The
// offscreen platform keeps the test independent of any display.
void ensureGuiApplication()
{
    static int argc = 1;
    static std::array<char, 12> name = {"test_render"};
    static std::array<char*, 2> argv = {name.data(), nullptr};
    static const std::unique_ptr<QGuiApplication> app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return std::make_unique<QGuiApplication>(argc, argv.data());
    }();
}

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
                                   .mesh = {}}));
            CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
            camera.orbit(40.0, 15.0);
        }

        // REQ-004-09, 11, 14, 16, 26: a mesh with faces, lines, points and a
        // selection, in every display mode and color mode.
        const auto mesh = fem::render::test::buildMesh(
            {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
             {2, 0, 0}, {3, 0, 0}},
            {{fem::core::ElementType::Hex8, {0, 1, 2, 3, 4, 5, 6, 7}},
             {fem::core::ElementType::Tri3, {1, 8, 2}},
             {fem::core::ElementType::Line2, {8, 9}},
             {fem::core::ElementType::Point1, {9}}},
            {{"solid", {0}}});
        const auto scene = fem::render::buildMeshScene(mesh);
        fem::render::Selection selection;
        std::uint64_t colorVersion = 0;
        for (const auto mode : {fem::render::DisplayMode::ShadedWithEdges,
                                fem::render::DisplayMode::Shaded,
                                fem::render::DisplayMode::Wireframe}) {
            for (const auto selectionMode :
                 {fem::render::SelectionMode::Elements, fem::render::SelectionMode::Nodes}) {
                selection.reset(selectionMode, *mesh);
                const std::vector<int> picked = {0, 1, 2};
                selection.add(picked);
                fem::render::MeshFrame frame;
                frame.scene = scene.get();
                frame.sceneVersion = 1;
                frame.colorVersion = ++colorVersion;
                frame.display.mode = mode;
                frame.display.color = selectionMode == fem::render::SelectionMode::Elements
                                          ? fem::render::ColorMode::ByGroup
                                          : fem::render::ColorMode::ByDimension;
                frame.display.dimensionVisible = {true, true, mode != fem::render::DisplayMode::Shaded, true};
                frame.selection = &selection;

                QRhiCommandBuffer* commandBuffer = nullptr;
                REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
                const bool rendered = renderer.render(commandBuffer, target.get(),
                                                      {.camera = &camera,
                                                       .pixelSize = size,
                                                       .devicePixelRatio = 1.0,
                                                       .showGrid = true,
                                                       .showAxes = true,
                                                       .mesh = frame});
                INFO(renderer.errorString().toStdString());
                CHECK(rendered);
                CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
            }
        }

        renderer.release();
        CHECK_FALSE(renderer.isInitialized());

        // It can be set up again after a release (e.g. a new QRhi).
        REQUIRE(renderer.initialize(rhi.get(), renderPass.get(), 1));
    }
}
