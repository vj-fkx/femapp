#include "gui_app.hpp"
#include "scene_renderer.hpp"
#include "symbol_layer.hpp"

#include <fem/render/camera.hpp>
#include <fem/render/symbols.hpp>

#include <catch2/catch_test_macros.hpp>
#include <rhi/qrhi.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

// Feature 010, T20 (design §5.6): REQ-010-09, 22. The molds of the glyphs,
// the instances of a SymbolSet and the pass that draws them.

namespace {

namespace r = fem::render;
namespace d = fem::render::detail;
using Eigen::Vector3d;

std::uint8_t bits(std::initializer_list<int> dofs)
{
    unsigned held = 0;
    for (const int dof : dofs) {
        held |= 1U << static_cast<unsigned>(dof);
    }
    return static_cast<std::uint8_t>(held);
}

std::size_t count(const d::SymbolInstances& all, d::Mold mold)
{
    return all.at(static_cast<std::size_t>(mold)).size();
}

} // namespace

TEST_CASE("the glyph of a support follows the dofs held", "[render][symbols]")
{
    // U V W RX RY RZ = 0..5
    CHECK(r::supportShape(bits({0, 1, 2, 3, 4, 5}), false).glyph == r::SupportGlyph::Fixed);
    CHECK(r::supportShape(bits({0, 1, 2}), false).glyph == r::SupportGlyph::Pinned);
    const r::SupportShape roller = r::supportShape(bits({0}), false);
    CHECK(roller.glyph == r::SupportGlyph::Roller);
    CHECK(roller.up == Vector3d::UnitX());
    CHECK(r::supportShape(bits({0, 1, 2, 5}), false).glyph == r::SupportGlyph::Generic);
    // Planar: only U, V and RZ count, warping and temperature never.
    CHECK(r::supportShape(bits({0, 1, 5}), true).glyph == r::SupportGlyph::Fixed);
    CHECK(r::supportShape(bits({0, 1, 7}), true).glyph == r::SupportGlyph::Pinned);
    CHECK(r::supportShape(bits({1}), true).up == Vector3d::UnitY());
    CHECK(r::supportShape(0, true).glyph == r::SupportGlyph::Generic);
}

TEST_CASE("the molds are lines of unit size", "[render][symbols]")
{
    for (std::size_t m = 0; m < d::kMoldCount; ++m) {
        INFO(m);
        const auto& vertices = d::moldVertices(static_cast<d::Mold>(m));
        REQUIRE_FALSE(vertices.empty());
        CHECK(vertices.size() % 2 == 0);
        for (const d::MoldVertex& v : vertices) {
            for (std::size_t k = 0; k < 3; ++k) {
                CHECK(std::abs(v.position.at(k)) <= 1.0F);
            }
        }
    }
    // The arrow ends at the node and comes from -x.
    const auto& arrow = d::moldVertices(d::Mold::Arrow);
    CHECK(arrow.at(0).position[0] == -1.0F);
    CHECK(arrow.at(1).position[0] == 0.0F);
    // The triad in the colors of the axes; the others take the instance's.
    CHECK((d::moldVertices(d::Mold::Triad).at(0).color >> 24U) == 0xFFU);
    CHECK(d::moldVertices(d::Mold::Pinned).at(0).color == 0U);
}

TEST_CASE("the instances of a symbol set", "[render][symbols]")
{
    r::SymbolSet set;
    set.supports = {{.at = Vector3d(0, 0, 0), .held = bits({0, 1, 2}), .planar = false},
                    {.at = Vector3d(4, 0, 0), .held = bits({1}), .planar = false}};
    set.forces = {{.at = Vector3d(2, 3, 0), .vector = Vector3d(0, -10, 0)},
                  {.at = Vector3d(2, 3, 0), .vector = Vector3d::Zero()}}; // not drawn
    set.moments = {{.at = Vector3d(2, 3, 0), .axis = Vector3d(0, 0, 5)}};
    set.distributed = {{.from = Vector3d(0, 3, 0),
                        .to = Vector3d(4, 3, 0),
                        .startValue = Vector3d(0, -2, 0),
                        .endValue = Vector3d(0, -4, 0)}};
    set.axes = {{.at = Vector3d(2, 3, 0), .axes = Eigen::Matrix3d::Identity()}};
    const Vector3d origin(2, 1.5, 0);
    const d::SymbolInstances all = d::instancesOf(set, origin);
    CHECK(count(all, d::Mold::Pinned) == 1);
    CHECK(count(all, d::Mold::Roller) == 1);
    CHECK(count(all, d::Mold::Arrow) == 1 + 8);
    CHECK(count(all, d::Mold::Arc) == 1);
    CHECK(count(all, d::Mold::Link) == 7);
    CHECK(count(all, d::Mold::Triad) == 1);

    // Positions from the origin; the force arrow along its direction.
    const d::SymbolInstance& force = all.at(static_cast<std::size_t>(d::Mold::Arrow)).front();
    CHECK(force.at == std::array<float, 3>{0.0F, 1.5F, 0.0F});
    CHECK(force.x == std::array<float, 3>{0.0F, -1.0F, 0.0F});
    // The distributed arrows as long as their value over the largest: the
    // last one is the largest.
    const auto& arrows = all.at(static_cast<std::size_t>(d::Mold::Arrow));
    const auto length = [](const d::SymbolInstance& i) {
        return std::hypot(i.x[0], i.x[1], i.x[2]);
    };
    CHECK(std::abs(length(arrows.at(1)) - 0.5F) <= 1e-6F);
    CHECK(std::abs(length(arrows.back()) - 1.0F) <= 1e-6F);
    // A link goes along the bar in the world: 4 / 7 per step.
    const d::SymbolInstance& link = all.at(static_cast<std::size_t>(d::Mold::Link)).front();
    CHECK(std::abs(link.y[0] - 4.0F / 7.0F) <= 1e-6F);
    // Every basis is right-handed.
    for (const auto& mold : all) {
        for (const d::SymbolInstance& i : mold) {
            const Vector3d x(i.x[0], i.x[1], i.x[2]);
            const Vector3d y(i.y[0], i.y[1], i.y[2]);
            const Vector3d z(i.z[0], i.z[1], i.z[2]);
            if (&mold != &all.at(static_cast<std::size_t>(d::Mold::Link))) {
                CHECK(x.cross(y).dot(z) > 0.0);
            }
        }
    }
}

// REQ-012-16: the reactions of a result: arrows ending at the node, in the
// sense of the reaction, and arcs, in their own color.
TEST_CASE("the reactions of a result as symbols", "[render][symbols]")
{
    r::SymbolSet set;
    CHECK(set.empty());
    set.forces = {{.at = Vector3d(2, 3, 0), .vector = Vector3d(0, -10, 0)}};
    set.reactionForces = {{.at = Vector3d(0, 0, 0), .vector = Vector3d(0, 25, 0)},
                          {.at = Vector3d(4, 0, 0), .vector = Vector3d::Zero()}}; // not drawn
    set.reactionMoments = {{.at = Vector3d(0, 0, 0), .axis = Vector3d(0, 0, -3)}};
    CHECK_FALSE(set.empty());
    const d::SymbolInstances all = d::instancesOf(set, Vector3d::Zero());
    const auto& arrows = all.at(static_cast<std::size_t>(d::Mold::Arrow));
    REQUIRE(arrows.size() == 2);
    const d::SymbolInstance& reaction = arrows.back();
    CHECK(reaction.color == d::kReactionColor);
    CHECK(arrows.front().color != d::kReactionColor);
    // The arrow ends at the node and points up, the sense of the reaction.
    CHECK(reaction.at == std::array<float, 3>{0.0F, 0.0F, 0.0F});
    CHECK(reaction.x == std::array<float, 3>{0.0F, 1.0F, 0.0F});
    const auto& arcs = all.at(static_cast<std::size_t>(d::Mold::Arc));
    REQUIRE(arcs.size() == 1);
    CHECK(arcs.front().color == d::kReactionColor);
    CHECK(arcs.front().x == std::array<float, 3>{0.0F, 0.0F, -1.0F});
}

TEST_CASE("the symbol pass draws with the Null backend", "[render][symbols]")
{
    fem::render::test::ensureGuiApplication();
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

    r::Camera camera;
    camera.setViewportSize(size.width(), size.height());
    r::SceneRenderer renderer;
    const bool initialized = renderer.initialize(rhi.get(), renderPass.get(), 1);
    INFO(renderer.errorString().toStdString());
    REQUIRE(initialized);

    r::SymbolSet set;
    set.supports = {{.at = Vector3d(0, 0, 0), .held = bits({0, 1, 2, 3, 4, 5}), .planar = false}};
    set.forces = {{.at = Vector3d(1, 1, 0), .vector = Vector3d(0, -1, 0)}};
    set.distributed = {{.from = Vector3d(0, 1, 0),
                        .to = Vector3d(2, 1, 0),
                        .startValue = Vector3d(0, -1, 0),
                        .endValue = Vector3d(0, -1, 0)}};
    // Shown, hidden, changed and none: each frame records.
    std::uint64_t version = 0;
    for (const bool visible : {true, false, true}) {
        for (const r::SymbolSet* symbols : std::array<const r::SymbolSet*, 2>{&set, nullptr}) {
            QRhiCommandBuffer* commandBuffer = nullptr;
            REQUIRE(rhi->beginOffscreenFrame(&commandBuffer) == QRhi::FrameOpSuccess);
            const bool rendered = renderer.render(
                commandBuffer, target.get(),
                {.camera = &camera,
                 .pixelSize = size,
                 .devicePixelRatio = 1.0,
                 .showGrid = false,
                 .showAxes = false,
                 .mesh = {},
                 .drawing = {},
                 .symbols = {.symbols = symbols, .version = ++version, .visible = visible}});
            INFO(renderer.errorString().toStdString());
            CHECK(rendered);
            CHECK(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
        }
    }
    renderer.release();
}

TEST_CASE("the symbol layer keeps the instances of the set", "[render][symbols]")
{
    fem::render::test::ensureGuiApplication();
    QRhiNullInitParams initParams;
    const std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Null, &initParams));
    REQUIRE(rhi != nullptr);
    const std::unique_ptr<QRhiTexture> color(
        rhi->newTexture(QRhiTexture::RGBA8, QSize(64, 64), 1, QRhiTexture::RenderTarget));
    REQUIRE(color->create());
    const std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget(
        QRhiTextureRenderTargetDescription(QRhiColorAttachment(color.get()))));
    const std::unique_ptr<QRhiRenderPassDescriptor> renderPass(
        target->newCompatibleRenderPassDescriptor());

    r::SymbolLayer layer;
    QString error;
    REQUIRE(layer.create(rhi.get(), renderPass.get(), 1, &error));
    r::SymbolSet set;
    set.moments = {{.at = Vector3d(1, 2, 3), .axis = Vector3d(1, 0, 0)},
                   {.at = Vector3d(3, 2, 1), .axis = Vector3d(0, 1, 0)}};
    r::Camera camera;
    camera.setViewportSize(64, 64);
    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
    REQUIRE(layer.prepare(updates, {.symbols = &set, .version = 1, .visible = true}, camera,
                          QSize(64, 64), 1.0, &error));
    updates->release();
    const auto counts = layer.instanceCounts();
    CHECK(counts.at(static_cast<std::size_t>(d::Mold::Arc)) == 2);
    CHECK(std::ranges::count(counts, 0) == static_cast<long>(d::kMoldCount) - 1);
}
