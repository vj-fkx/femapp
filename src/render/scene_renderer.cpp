#include "scene_renderer.hpp"

#include "axis_gizmo_mesh.hpp"
#include "axis_labels.hpp"
#include "rhi_util.hpp"

#include <fem/render/grid_spacing.hpp>

#include <QColor>
#include <QFile>
#include <QMatrix4x4>

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace fem::render {

namespace {

constexpr QColor kBackgroundColor(38, 41, 48);

// The grid quad reaches this many camera distances away from the target.
constexpr double kGridExtentFactor = 40.0;

// Axis indicator: size and margin in logical pixels, and the half-extent of
// its orthographic view volume, in arrow lengths.
constexpr double kGizmoSizePx = 96.0;
constexpr double kGizmoMarginPx = 8.0;
constexpr float kGizmoHalfExtent = 1.5F;
constexpr double kLabelHalfSizePx = 7.0;
constexpr int kLabelCellBasePx = 32;
// The indicator is drawn in a thin slice at the front of the depth range so
// that it always stays on top of the scene.
constexpr float kGizmoMaxDepth = 0.01F;

using detail::alphaBlend;
using detail::copyMatrix;
using detail::kMatrixFloats;
using detail::kVectorFloats;
using detail::loadShader;
using detail::toOpenGlDepth;
using detail::toQt;
constexpr int kGridUniformFloats = kMatrixFloats + 3 * kVectorFloats;
constexpr int kGizmoUniformFloats = 2 * kMatrixFloats;
constexpr int kLabelUniformFloats = 2 * kMatrixFloats + kVectorFloats;

template <std::size_t N>
constexpr quint32 byteSize(const std::array<float, N>& /*data*/)
{
    return static_cast<quint32>(N * sizeof(float));
}

} // namespace

SceneRenderer::SceneRenderer() = default;

SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::initialize(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount)
{
    release();
    m_error.clear();
    m_rhi = rhi;

    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
    const bool created = createGrid(renderPass, sampleCount, updates) &&
                         createGizmo(renderPass, sampleCount, updates) &&
                         createLabels(renderPass, sampleCount, updates) &&
                         m_mesh.create(rhi, renderPass, sampleCount, &m_error);
    if (!created) {
        updates->release();
        release();
        return false;
    }

    // The static buffers are uploaded with the first frame.
    m_pendingUpdates = updates;
    return true;
}

bool SceneRenderer::createGrid(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                               QRhiResourceUpdateBatch* updates)
{
    const QShader vertexShader = loadShader(QStringLiteral("grid.vert"), &m_error);
    const QShader fragmentShader = loadShader(QStringLiteral("grid.frag"), &m_error);
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        return false;
    }

    // Triangle strip covering the square [-1, 1] x [-1, 1].
    constexpr std::array<float, 8> kCorners = {-1.0F, -1.0F, 1.0F, -1.0F, -1.0F, 1.0F, 1.0F, 1.0F};
    m_gridVertices.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, byteSize(kCorners)));
    m_gridUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                          kGridUniformFloats * sizeof(float)));
    if (!m_gridVertices->create() || !m_gridUniforms->create()) {
        m_error = QStringLiteral("could not create the grid buffers");
        return false;
    }
    updates->uploadStaticBuffer(m_gridVertices.get(), kCorners.data());

    m_gridBindings.reset(m_rhi->newShaderResourceBindings());
    m_gridBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        m_gridUniforms.get())});
    if (!m_gridBindings->create()) {
        m_error = QStringLiteral("could not create the grid resource bindings");
        return false;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{2 * sizeof(float)}});
    layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}});

    m_gridPipeline.reset(m_rhi->newGraphicsPipeline());
    m_gridPipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    m_gridPipeline->setVertexInputLayout(layout);
    m_gridPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_gridPipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_gridPipeline->setTargetBlends({alphaBlend()});
    // The grid is tested against the scene but does not occlude anything.
    m_gridPipeline->setDepthTest(true);
    m_gridPipeline->setDepthWrite(false);
    m_gridPipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
    m_gridPipeline->setSampleCount(sampleCount);
    m_gridPipeline->setShaderResourceBindings(m_gridBindings.get());
    m_gridPipeline->setRenderPassDescriptor(renderPass);
    if (!m_gridPipeline->create()) {
        m_error = QStringLiteral("could not create the grid pipeline");
        return false;
    }
    return true;
}

bool SceneRenderer::createGizmo(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                                QRhiResourceUpdateBatch* updates)
{
    const QShader vertexShader = loadShader(QStringLiteral("gizmo.vert"), &m_error);
    const QShader fragmentShader = loadShader(QStringLiteral("gizmo.frag"), &m_error);
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        return false;
    }

    const std::vector<GizmoVertex> mesh = buildAxisGizmoMesh();
    m_gizmoVertexCount = static_cast<int>(mesh.size());
    m_gizmoVertices.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                         static_cast<quint32>(mesh.size() * sizeof(GizmoVertex))));
    m_gizmoUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                           kGizmoUniformFloats * sizeof(float)));
    if (!m_gizmoVertices->create() || !m_gizmoUniforms->create()) {
        m_error = QStringLiteral("could not create the axis indicator buffers");
        return false;
    }
    updates->uploadStaticBuffer(m_gizmoVertices.get(), mesh.data());

    m_gizmoBindings.reset(m_rhi->newShaderResourceBindings());
    m_gizmoBindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, m_gizmoUniforms.get())});
    if (!m_gizmoBindings->create()) {
        m_error = QStringLiteral("could not create the axis indicator resource bindings");
        return false;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{sizeof(GizmoVertex)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         static_cast<quint32>(offsetof(GizmoVertex, position))},
        {0, 1, QRhiVertexInputAttribute::Float3,
         static_cast<quint32>(offsetof(GizmoVertex, normal))},
        {0, 2, QRhiVertexInputAttribute::Float3,
         static_cast<quint32>(offsetof(GizmoVertex, color))},
    });

    m_gizmoPipeline.reset(m_rhi->newGraphicsPipeline());
    m_gizmoPipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    m_gizmoPipeline->setVertexInputLayout(layout);
    m_gizmoPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_gizmoPipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_gizmoPipeline->setDepthTest(true);
    m_gizmoPipeline->setDepthWrite(true);
    m_gizmoPipeline->setSampleCount(sampleCount);
    m_gizmoPipeline->setShaderResourceBindings(m_gizmoBindings.get());
    m_gizmoPipeline->setRenderPassDescriptor(renderPass);
    if (!m_gizmoPipeline->create()) {
        m_error = QStringLiteral("could not create the axis indicator pipeline");
        return false;
    }
    return true;
}

bool SceneRenderer::createLabels(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                                 QRhiResourceUpdateBatch* updates)
{
    const QShader vertexShader = loadShader(QStringLiteral("label.vert"), &m_error);
    const QShader fragmentShader = loadShader(QStringLiteral("label.frag"), &m_error);
    if (!vertexShader.isValid() || !fragmentShader.isValid()) {
        return false;
    }

    const std::vector<LabelVertex> quads = buildAxisLabelQuads();
    m_labelVertexCount = static_cast<int>(quads.size());
    m_labelVertices.reset(
        m_rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                         static_cast<quint32>(quads.size() * sizeof(LabelVertex))));
    m_labelUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                           kLabelUniformFloats * sizeof(float)));
    m_labelSampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                           QRhiSampler::None, QRhiSampler::ClampToEdge,
                                           QRhiSampler::ClampToEdge));
    if (!m_labelVertices->create() || !m_labelUniforms->create() || !m_labelSampler->create()) {
        m_error = QStringLiteral("could not create the axis label buffers");
        return false;
    }
    updates->uploadStaticBuffer(m_labelVertices.get(), quads.data());

    m_labelAtlasCellSize = 0;
    if (!updateLabelAtlas(1.0, updates)) {
        return false;
    }

    m_labelBindings.reset(m_rhi->newShaderResourceBindings());
    m_labelBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage,
                                                 m_labelUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_labelAtlas.get(), m_labelSampler.get()),
    });
    if (!m_labelBindings->create()) {
        m_error = QStringLiteral("could not create the axis label resource bindings");
        return false;
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{sizeof(LabelVertex)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3,
         static_cast<quint32>(offsetof(LabelVertex, anchor))},
        {0, 1, QRhiVertexInputAttribute::Float2,
         static_cast<quint32>(offsetof(LabelVertex, corner))},
        {0, 2, QRhiVertexInputAttribute::Float2,
         static_cast<quint32>(offsetof(LabelVertex, texCoord))},
        {0, 3, QRhiVertexInputAttribute::Float3,
         static_cast<quint32>(offsetof(LabelVertex, color))},
    });

    m_labelPipeline.reset(m_rhi->newGraphicsPipeline());
    m_labelPipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, vertexShader}, {QRhiShaderStage::Fragment, fragmentShader}});
    m_labelPipeline->setVertexInputLayout(layout);
    m_labelPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_labelPipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_labelPipeline->setTargetBlends({alphaBlend()});
    // Labels are always readable, even when an arrow passes in front of them.
    m_labelPipeline->setDepthTest(false);
    m_labelPipeline->setDepthWrite(false);
    m_labelPipeline->setSampleCount(sampleCount);
    m_labelPipeline->setShaderResourceBindings(m_labelBindings.get());
    m_labelPipeline->setRenderPassDescriptor(renderPass);
    if (!m_labelPipeline->create()) {
        m_error = QStringLiteral("could not create the axis label pipeline");
        return false;
    }
    return true;
}

bool SceneRenderer::updateLabelAtlas(double devicePixelRatio, QRhiResourceUpdateBatch* updates)
{
    const int cellSize =
        kLabelCellBasePx * std::max(1, static_cast<int>(std::ceil(devicePixelRatio)));
    if (cellSize == m_labelAtlasCellSize) {
        return true;
    }

    const QImage atlas = buildAxisLabelAtlas(cellSize);
    if (!m_labelAtlas) {
        m_labelAtlas.reset(m_rhi->newTexture(QRhiTexture::RGBA8, atlas.size()));
    } else {
        // Re-creating in place keeps the resource bindings valid.
        m_labelAtlas->setPixelSize(atlas.size());
    }
    if (!m_labelAtlas->create()) {
        m_error = QStringLiteral("could not create the axis label texture");
        return false;
    }
    updates->uploadTexture(m_labelAtlas.get(), atlas);
    m_labelAtlasCellSize = cellSize;
    return true;
}

void SceneRenderer::updateGridUniforms(const FrameParams& params, QRhiResourceUpdateBatch* updates)
{
    const Camera& camera = *params.camera;
    const Vec3& target = camera.target();
    const GridSpacing spacing = gridSpacing(camera.worldUnitsPerPixel());
    const double extent = kGridExtentFactor * (camera.distance() + std::abs(target.z()));

    const QMatrix4x4 viewProjection =
        m_rhi->clipSpaceCorrMatrix() *
        toQt(toOpenGlDepth(camera.projectionMatrix()) * camera.viewMatrixRelativeToTarget());

    std::array<float, kGridUniformFloats> data{};
    copyMatrix(viewProjection, data.data());
    // The grid pattern is periodic, so only the target position modulo the
    // major spacing is needed. Reducing it in double precision keeps the lines
    // stable when the target is far from the origin.
    std::size_t i = kMatrixFloats;
    data.at(i++) = static_cast<float>(std::fmod(target.x(), spacing.major));
    data.at(i++) = static_cast<float>(std::fmod(target.y(), spacing.major));
    data.at(i++) = static_cast<float>(-target.z());
    data.at(i++) = static_cast<float>(extent);
    data.at(i++) = static_cast<float>(spacing.minor);
    data.at(i++) = static_cast<float>(spacing.major);
    data.at(i++) = static_cast<float>(spacing.minorFade);
    // REQ-002-22, 26: the grid lines only when the grid is on; the axis lines
    // always.
    data.at(i++) = params.showGrid ? 1.0F : 0.0F;
    data.at(i++) = static_cast<float>(target.x());
    data.at(i++) = static_cast<float>(target.y());

    updates->updateDynamicBuffer(m_gridUniforms.get(), 0, byteSize(data), data.data());
}

void SceneRenderer::updateGizmoUniforms(const FrameParams& params, QRhiResourceUpdateBatch* updates)
{
    // Only the orientation of the camera matters for the indicator.
    Mat4 rotation = params.camera->viewMatrixRelativeToTarget();
    rotation(2, 3) = 0.0;
    const QMatrix4x4 rotationQt = toQt(rotation);

    QMatrix4x4 projection;
    projection.ortho(-kGizmoHalfExtent, kGizmoHalfExtent, -kGizmoHalfExtent, kGizmoHalfExtent,
                     -kGizmoHalfExtent, kGizmoHalfExtent);
    const QMatrix4x4 modelViewProjection = projection * rotationQt;
    const QMatrix4x4 correction = m_rhi->clipSpaceCorrMatrix();

    std::array<float, kGizmoUniformFloats> gizmo{};
    copyMatrix(correction * modelViewProjection, gizmo.data());
    copyMatrix(rotationQt, gizmo.data() + kMatrixFloats);
    updates->updateDynamicBuffer(m_gizmoUniforms.get(), 0, byteSize(gizmo), gizmo.data());

    std::array<float, kLabelUniformFloats> labels{};
    copyMatrix(correction, labels.data());
    copyMatrix(modelViewProjection, labels.data() + kMatrixFloats);
    const auto halfSize = static_cast<float>(2.0 * kLabelHalfSizePx / kGizmoSizePx);
    const std::size_t halfSizeOffset = static_cast<std::size_t>(kMatrixFloats) * 2;
    labels.at(halfSizeOffset) = halfSize;
    labels.at(halfSizeOffset + 1) = halfSize;
    updates->updateDynamicBuffer(m_labelUniforms.get(), 0, byteSize(labels), labels.data());
}

bool SceneRenderer::render(QRhiCommandBuffer* commandBuffer, QRhiRenderTarget* target,
                           const FrameParams& params)
{
    if (m_rhi == nullptr || params.camera == nullptr) {
        return true;
    }

    QRhiResourceUpdateBatch* updates = m_pendingUpdates;
    m_pendingUpdates = nullptr;
    if (updates == nullptr) {
        updates = m_rhi->nextResourceUpdateBatch();
    }

    const auto gizmoSize = static_cast<float>(std::round(kGizmoSizePx * params.devicePixelRatio));
    const auto gizmoMargin =
        static_cast<float>(std::round(kGizmoMarginPx * params.devicePixelRatio));
    const auto width = static_cast<float>(params.pixelSize.width());
    const auto height = static_cast<float>(params.pixelSize.height());

    const bool drawAxes = params.showAxes && width >= gizmoSize + gizmoMargin &&
                          height >= gizmoSize + gizmoMargin &&
                          updateLabelAtlas(params.devicePixelRatio, updates);
    // The grid pass also draws the axis lines, so it always runs.
    updateGridUniforms(params, updates);
    if (drawAxes) {
        updateGizmoUniforms(params, updates);
    }
    if (!m_mesh.prepare(updates, params.mesh, *params.camera, params.pixelSize,
                        params.devicePixelRatio, &m_error)) {
        updates->release();
        return false;
    }

    // The scene uses the depth range above the thin slice kept for the axis
    // indicator, so that the indicator always stays in front.
    const QRhiViewport sceneViewport(0.0F, 0.0F, width, height, kGizmoMaxDepth, 1.0F);

    commandBuffer->beginPass(target, kBackgroundColor, {1.0F, 0}, updates);

    m_mesh.draw(commandBuffer, sceneViewport, params.mesh.display);

    {
        commandBuffer->setGraphicsPipeline(m_gridPipeline.get());
        commandBuffer->setViewport(sceneViewport);
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput input(m_gridVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &input);
        commandBuffer->draw(4);
    }

    m_mesh.drawOverlay(commandBuffer, sceneViewport);

    if (drawAxes) {
        // QRhi viewports have their origin at the bottom-left corner.
        const QRhiViewport corner(gizmoMargin, gizmoMargin, gizmoSize, gizmoSize, 0.0F,
                                  kGizmoMaxDepth);

        commandBuffer->setGraphicsPipeline(m_gizmoPipeline.get());
        commandBuffer->setViewport(corner);
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput gizmoInput(m_gizmoVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &gizmoInput);
        commandBuffer->draw(static_cast<quint32>(m_gizmoVertexCount));

        commandBuffer->setGraphicsPipeline(m_labelPipeline.get());
        commandBuffer->setViewport(corner);
        commandBuffer->setShaderResources();
        const QRhiCommandBuffer::VertexInput labelInput(m_labelVertices.get(), 0);
        commandBuffer->setVertexInput(0, 1, &labelInput);
        commandBuffer->draw(static_cast<quint32>(m_labelVertexCount));
    }

    commandBuffer->endPass();
    return true;
}

void SceneRenderer::release()
{
    m_mesh.release();
    if (m_pendingUpdates != nullptr) {
        m_pendingUpdates->release();
        m_pendingUpdates = nullptr;
    }

    m_labelPipeline.reset();
    m_labelBindings.reset();
    m_labelSampler.reset();
    m_labelAtlas.reset();
    m_labelUniforms.reset();
    m_labelVertices.reset();
    m_labelVertexCount = 0;
    m_labelAtlasCellSize = 0;

    m_gizmoPipeline.reset();
    m_gizmoBindings.reset();
    m_gizmoUniforms.reset();
    m_gizmoVertices.reset();
    m_gizmoVertexCount = 0;

    m_gridPipeline.reset();
    m_gridBindings.reset();
    m_gridUniforms.reset();
    m_gridVertices.reset();

    m_rhi = nullptr;
}

} // namespace fem::render
