#include "drawing_gpu.hpp"

#include "rhi_util.hpp"

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace fem::render {

namespace {

using detail::copyMatrix;
using detail::kMatrixFloats;
using detail::kVectorFloats;
using detail::loadShader;
using detail::toOpenGlDepth;
using detail::toQt;

using Rgba = std::uint32_t; // 0xAABBGGRR

constexpr int kUniformFloats = 2 * kMatrixFloats + 4 * kVectorFloats;
constexpr double kPointSizePx = 6.0;

// Flags in the alpha channel, as in the mesh shaders: bit 0 selected, bit 1
// "a line in its own color".
constexpr Rgba kSelectedFlag = 1U << 24U;
constexpr Rgba kOwnColorFlag = 2U << 24U;

constexpr Rgba rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept
{
    return r | (g << 8U) | (b << 16U);
}

// Design 005 §5.5: off-white entities, a dimmer preview.
constexpr Rgba kEntityColor = rgb(232, 236, 240);
constexpr Rgba kPreviewColor = rgb(150, 160, 175);

constexpr std::array<float, 4> kEdgeColor = {0.12F, 0.13F, 0.15F, 1.0F};
constexpr std::array<float, 4> kHighlightColor = {1.0F, 0.55F, 0.0F, 1.0F};
constexpr std::array<float, 12> kCorners = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};

struct LineVertex
{
    std::array<float, 3> position;
};

struct PointInstance
{
    std::array<float, 3> position;
    Rgba color;
};

// Lines (positions and colors, two vertices each) and points of a set of
// entities, relative to `origin`.
struct Geometry
{
    std::vector<LineVertex> lines;
    std::vector<Rgba> lineColors;
    std::vector<PointInstance> points;

    void add(const geom::Entity& entity, const Eigen::Vector3d& origin, Rgba color)
    {
        const auto relative = [&](const geom::Vec3& p) {
            const Eigen::Vector3f r = (p - origin).cast<float>();
            return std::array<float, 3>{r.x(), r.y(), r.z()};
        };
        const auto segments = geom::segmentsOf(entity);
        if (segments.empty()) {
            for (const auto& p : geom::verticesOf(entity)) {
                points.push_back({.position = relative(p), .color = color});
            }
            return;
        }
        for (const geom::Segment& segment : segments) {
            lines.push_back({relative(segment.a)});
            lines.push_back({relative(segment.b)});
            lineColors.insert(lineColors.end(), 2, color | kOwnColorFlag);
        }
    }
};

template <typename T>
quint32 bytes(const std::vector<T>& data)
{
    return static_cast<quint32>(data.size() * sizeof(T));
}

} // namespace

struct DrawingGpu::Impl
{
    QRhi* rhi = nullptr;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiBuffer> corners;
    std::unique_ptr<QRhiGraphicsPipeline> linePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> pointPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> previewLinePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> previewPointPipeline;
    bool uploaded = false;

    struct Buffers
    {
        std::unique_ptr<QRhiBuffer> lines;
        std::unique_ptr<QRhiBuffer> lineColors;
        std::unique_ptr<QRhiBuffer> points;
        quint32 lineVertices = 0;
        quint32 pointCount = 0;
    };
    Buffers entities;
    Buffers preview;

    const geom::Drawing* drawing = nullptr;
    std::uint64_t revision = 0;
    std::uint64_t selectionVersion = 0;
    std::uint64_t previewVersion = 0;
    bool hasEntities = false;
    Eigen::Vector3d origin = Eigen::Vector3d::Zero();

    [[nodiscard]] QRhiGraphicsPipeline* makePipeline(const QShader& vertex, const QShader& fragment,
                                                     const QRhiVertexInputLayout& layout,
                                                     QRhiGraphicsPipeline::Topology topology,
                                                     QRhiRenderPassDescriptor* renderPass,
                                                     int sampleCount, bool depthTest) const
    {
        QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        pipeline->setVertexInputLayout(layout);
        pipeline->setTopology(topology);
        pipeline->setCullMode(QRhiGraphicsPipeline::None);
        pipeline->setDepthTest(depthTest);
        pipeline->setDepthWrite(depthTest);
        pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        pipeline->setSampleCount(sampleCount);
        pipeline->setShaderResourceBindings(bindings.get());
        pipeline->setRenderPassDescriptor(renderPass);
        return pipeline;
    }

    [[nodiscard]] std::unique_ptr<QRhiBuffer> staticBuffer(quint32 size, const void* data,
                                                           QRhiResourceUpdateBatch* updates,
                                                           QString* error) const
    {
        if (size == 0) {
            return {};
        }
        std::unique_ptr<QRhiBuffer> buffer(
            rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer, size));
        if (!buffer->create()) {
            *error = QStringLiteral("could not create a drawing buffer of %1 bytes").arg(size);
            return {};
        }
        updates->uploadStaticBuffer(buffer.get(), data);
        return buffer;
    }

    [[nodiscard]] bool upload(Buffers& into, const Geometry& geometry,
                              QRhiResourceUpdateBatch* updates, QString* error) const
    {
        into.lines = staticBuffer(bytes(geometry.lines), geometry.lines.data(), updates, error);
        into.lineColors =
            staticBuffer(bytes(geometry.lineColors), geometry.lineColors.data(), updates, error);
        into.points = staticBuffer(bytes(geometry.points), geometry.points.data(), updates, error);
        into.lineVertices = static_cast<quint32>(geometry.lines.size());
        into.pointCount = static_cast<quint32>(geometry.points.size());
        return error->isEmpty();
    }

    void drawBuffers(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport,
                     const Buffers& buffers, QRhiGraphicsPipeline* lines,
                     QRhiGraphicsPipeline* points) const
    {
        if (buffers.lines && buffers.lineColors && buffers.lineVertices > 0) {
            commandBuffer->setGraphicsPipeline(lines);
            commandBuffer->setViewport(viewport);
            commandBuffer->setShaderResources();
            const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
                {buffers.lines.get(), 0},
                {buffers.lineColors.get(), 0},
            }};
            commandBuffer->setVertexInput(0, 2, inputs.data());
            commandBuffer->draw(buffers.lineVertices);
        }
        if (buffers.points && buffers.pointCount > 0) {
            commandBuffer->setGraphicsPipeline(points);
            commandBuffer->setViewport(viewport);
            commandBuffer->setShaderResources();
            const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
                {corners.get(), 0},
                {buffers.points.get(), 0},
            }};
            commandBuffer->setVertexInput(0, 2, inputs.data());
            commandBuffer->draw(6, buffers.pointCount);
        }
    }
};

DrawingGpu::DrawingGpu()
    : m_impl(std::make_unique<Impl>())
{}

DrawingGpu::~DrawingGpu() = default;

bool DrawingGpu::create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                        QString* error)
{
    release();
    Impl& d = *m_impl;
    d.rhi = rhi;

    const QShader lineVert = loadShader(QStringLiteral("mesh_line.vert"), error);
    const QShader lineFrag = loadShader(QStringLiteral("mesh_line.frag"), error);
    const QShader pointVert = loadShader(QStringLiteral("mesh_point.vert"), error);
    const QShader pointFrag = loadShader(QStringLiteral("mesh_point.frag"), error);
    if (!lineVert.isValid() || !lineFrag.isValid() || !pointVert.isValid() ||
        !pointFrag.isValid()) {
        return false;
    }

    d.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                    kUniformFloats * sizeof(float)));
    d.corners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                   static_cast<quint32>(sizeof(kCorners))));
    if (!d.uniforms->create() || !d.corners->create()) {
        *error = QStringLiteral("could not create the drawing uniform buffers");
        return false;
    }
    d.bindings.reset(rhi->newShaderResourceBindings());
    d.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        d.uniforms.get())});
    if (!d.bindings->create()) {
        *error = QStringLiteral("could not create the drawing resource bindings");
        return false;
    }

    QRhiVertexInputLayout lines;
    lines.setBindings({{sizeof(LineVertex)}, {sizeof(Rgba)}});
    lines.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                         {1, 1, QRhiVertexInputAttribute::UNormByte4, 0}});
    QRhiVertexInputLayout points;
    points.setBindings(
        {{2 * sizeof(float)}, {sizeof(PointInstance), QRhiVertexInputBinding::PerInstance}});
    points.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                          {1, 1, QRhiVertexInputAttribute::Float3,
                           static_cast<quint32>(offsetof(PointInstance, position))},
                          {1, 2, QRhiVertexInputAttribute::UNormByte4,
                           static_cast<quint32>(offsetof(PointInstance, color))}});

    d.linePipeline.reset(d.makePipeline(lineVert, lineFrag, lines, QRhiGraphicsPipeline::Lines,
                                        renderPass, sampleCount, true));
    d.pointPipeline.reset(d.makePipeline(pointVert, pointFrag, points,
                                         QRhiGraphicsPipeline::Triangles, renderPass, sampleCount,
                                         true));
    // The preview stays visible through the model.
    d.previewLinePipeline.reset(d.makePipeline(
        lineVert, lineFrag, lines, QRhiGraphicsPipeline::Lines, renderPass, sampleCount, false));
    d.previewPointPipeline.reset(d.makePipeline(pointVert, pointFrag, points,
                                                QRhiGraphicsPipeline::Triangles, renderPass,
                                                sampleCount, false));
    if (!d.linePipeline->create() || !d.pointPipeline->create() ||
        !d.previewLinePipeline->create() || !d.previewPointPipeline->create()) {
        *error = QStringLiteral("could not create the drawing pipelines");
        return false;
    }
    d.uploaded = false;
    return true;
}

void DrawingGpu::release()
{
    *m_impl = Impl{};
}

bool DrawingGpu::prepare(QRhiResourceUpdateBatch* updates, const DrawingFrame& frame,
                         const Camera& camera, QSize pixelSize, double devicePixelRatio,
                         QString* error)
{
    Impl& d = *m_impl;
    if (d.rhi == nullptr) {
        return false;
    }
    if (!d.uploaded) {
        updates->uploadStaticBuffer(d.corners.get(), kCorners.data());
        d.uploaded = true;
    }

    const geom::Drawing* drawing = frame.drawing;
    const bool changed = drawing != d.drawing ||
                         (drawing != nullptr && drawing->revision() != d.revision) ||
                         frame.selectionVersion != d.selectionVersion;
    if (changed) {
        Geometry geometry;
        if (drawing != nullptr) {
            const auto box = drawing->bounds();
            d.origin = box ? Eigen::Vector3d(0.5 * (box->min + box->max)) : Eigen::Vector3d::Zero();
            const auto& selected = frame.selection;
            for (const geom::Entity& entity : drawing->entities()) {
                const bool isSelected = selected != nullptr &&
                                        std::ranges::find(*selected, entity.id) != selected->end();
                geometry.add(entity, d.origin, kEntityColor | (isSelected ? kSelectedFlag : 0U));
                // REQ-005-25: the vertices of a selected line or polyline.
                if (isSelected && !geom::segmentsOf(entity).empty()) {
                    for (const auto& vertex : geom::verticesOf(entity)) {
                        geometry.add(
                            {.id = entity.id, .shape = geom::PointEntity{.position = vertex}},
                            d.origin, kEntityColor | kSelectedFlag);
                    }
                }
            }
        }
        if (!d.upload(d.entities, geometry, updates, error)) {
            return false;
        }
        d.drawing = drawing;
        d.revision = drawing != nullptr ? drawing->revision() : 0;
        d.selectionVersion = frame.selectionVersion;
        d.previewVersion = frame.previewVersion + 1; // origin may have moved
    }
    if (d.previewVersion != frame.previewVersion) {
        Geometry geometry;
        if (frame.preview != nullptr) {
            for (const geom::Entity& ghost : frame.preview->ghosts) {
                geometry.add(ghost, d.origin, kPreviewColor);
            }
            if (frame.preview->rubberBand) {
                const auto& [from, to] = *frame.preview->rubberBand;
                geometry.add(
                    {.id = geom::EntityId{}, .shape = geom::LineEntity{.start = from, .end = to}},
                    d.origin, kPreviewColor);
            }
        }
        if (!d.upload(d.preview, geometry, updates, error)) {
            return false;
        }
        d.previewVersion = frame.previewVersion;
    }
    d.hasEntities = drawing != nullptr;

    // Uniforms, every frame: positions are relative to the drawing origin, and
    // the offset to the camera target is computed in double (REQ-005-33).
    const Mat4 view = camera.viewMatrixRelativeToTarget();
    const QMatrix4x4 viewProjection =
        d.rhi->clipSpaceCorrMatrix() * toQt(toOpenGlDepth(camera.projectionMatrix()) * view);
    const Eigen::Vector3d offset = d.origin - camera.target();

    std::array<float, kUniformFloats> data{};
    copyMatrix(viewProjection, data.data());
    copyMatrix(toQt(view), data.data() + kMatrixFloats);
    std::size_t i = static_cast<std::size_t>(kMatrixFloats) * 2;
    data.at(i++) = static_cast<float>(offset.x());
    data.at(i++) = static_cast<float>(offset.y());
    data.at(i++) = static_cast<float>(offset.z());
    data.at(i++) = 0.0F;
    data.at(i++) = static_cast<float>(pixelSize.width());
    data.at(i++) = static_cast<float>(pixelSize.height());
    data.at(i++) = static_cast<float>(kPointSizePx * devicePixelRatio);
    data.at(i++) = 1.0F; // lines in their own color
    for (const float c : kEdgeColor) {
        data.at(i++) = c;
    }
    for (const float c : kHighlightColor) {
        data.at(i++) = c;
    }
    updates->updateDynamicBuffer(d.uniforms.get(), 0, static_cast<quint32>(sizeof(data)),
                                 data.data());
    return true;
}

void DrawingGpu::draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const
{
    const Impl& d = *m_impl;
    if (d.rhi == nullptr) {
        return;
    }
    d.drawBuffers(commandBuffer, viewport, d.entities, d.linePipeline.get(), d.pointPipeline.get());
    d.drawBuffers(commandBuffer, viewport, d.preview, d.previewLinePipeline.get(),
                  d.previewPointPipeline.get());
}

} // namespace fem::render
