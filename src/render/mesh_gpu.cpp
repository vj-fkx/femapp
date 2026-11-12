#include "mesh_gpu.hpp"

#include "rhi_util.hpp"

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace fem::render {

namespace {

using detail::copyMatrix;
using detail::kMatrixFloats;
using detail::kVectorFloats;
using detail::loadShader;
using detail::toOpenGlDepth;
using detail::toQt;

constexpr int kUniformFloats = 2 * kMatrixFloats + 4 * kVectorFloats;
constexpr double kPointSizePx = 6.0;

// Flags carried in the alpha channel of the colors (see the shaders).
constexpr Rgba kSelectedFlag = 1U << 24U;
constexpr Rgba kElementLineFlag = 2U << 24U;

constexpr std::array<float, 4> kEdgeColor = {0.12F, 0.13F, 0.15F, 1.0F};
constexpr std::array<float, 4> kHighlightColor = {1.0F, 0.55F, 0.0F, 1.0F};
// REQ-004-34: shown nodes, black (the selected ones get the highlight on
// top).
constexpr Rgba kNodeColor = 0x00000000U;

// Two triangles covering the square [-1, 1]^2, for the point instances.
constexpr std::array<float, 12> kCorners = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};

struct PointInstance
{
    std::array<float, 3> position;
    Rgba color;
};

struct SegmentInstance
{
    std::array<float, 3> from;
    std::array<float, 3> to;
    Rgba color;
};

std::array<float, 3> toArray(const Eigen::Vector3f& p)
{
    return {p.x(), p.y(), p.z()};
}

template <typename T>
quint32 bytes(const std::vector<T>& data)
{
    return static_cast<quint32>(data.size() * sizeof(T));
}

} // namespace

struct MeshGpu::Impl
{
    QRhi* rhi = nullptr;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> facePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> linePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> pointPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> outlinePipeline;
    std::unique_ptr<QRhiBuffer> corners;

    std::unique_ptr<QRhiBuffer> trianglePositions;
    std::unique_ptr<QRhiBuffer> triangleColors;
    std::unique_ptr<QRhiBuffer> linePositions;
    std::unique_ptr<QRhiBuffer> lineColors;
    std::unique_ptr<QRhiBuffer> points;
    std::unique_ptr<QRhiBuffer> selectedNodes;
    std::unique_ptr<QRhiBuffer> selectedEdges;
    std::unique_ptr<QRhiBuffer> outlines;
    quint32 outlineCount = 0;
    std::unique_ptr<QRhiBuffer> shownNodes;
    quint32 pointCount = 0;
    quint32 selectedNodeCount = 0;
    quint32 selectedEdgeCount = 0;
    quint32 shownNodeCount = 0;

    const MeshScene* scene = nullptr;
    std::array<Range, 2> triangleRanges{};
    std::array<Range, 3> lineRanges{};
    std::uint64_t sceneVersion = 0;
    std::uint64_t colorVersion = 0;
    std::uint64_t nodeVersion = 0;
    bool uploaded = false;

    [[nodiscard]] QRhiGraphicsPipeline* makePipeline(const QShader& vertex, const QShader& fragment,
                                                     const QRhiVertexInputLayout& layout,
                                                     QRhiGraphicsPipeline::Topology topology,
                                                     QRhiRenderPassDescriptor* renderPass,
                                                     int sampleCount) const
    {
        QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        pipeline->setVertexInputLayout(layout);
        pipeline->setTopology(topology);
        pipeline->setCullMode(QRhiGraphicsPipeline::None);
        pipeline->setDepthTest(true);
        pipeline->setDepthWrite(true);
        pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        pipeline->setSampleCount(sampleCount);
        pipeline->setShaderResourceBindings(bindings.get());
        pipeline->setRenderPassDescriptor(renderPass);
        return pipeline;
    }

    // Static vertex buffer with `data`, or nothing when it is empty.
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
            *error = QStringLiteral("could not create a mesh buffer of %1 bytes").arg(size);
            return {};
        }
        updates->uploadStaticBuffer(buffer.get(), data);
        return buffer;
    }
};

MeshGpu::MeshGpu()
    : m_impl(std::make_unique<Impl>())
{}

MeshGpu::~MeshGpu() = default;

bool MeshGpu::create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                     QString* error)
{
    release();
    Impl& d = *m_impl;
    d.rhi = rhi;

    const QShader faceVert = loadShader(QStringLiteral("mesh_face.vert"), error);
    const QShader faceFrag = loadShader(QStringLiteral("mesh_face.frag"), error);
    const QShader lineVert = loadShader(QStringLiteral("mesh_line.vert"), error);
    const QShader lineFrag = loadShader(QStringLiteral("mesh_line.frag"), error);
    const QShader pointVert = loadShader(QStringLiteral("mesh_point.vert"), error);
    const QShader pointFrag = loadShader(QStringLiteral("mesh_point.frag"), error);
    const QShader segmentVert = loadShader(QStringLiteral("mesh_segment.vert"), error);
    if (!faceVert.isValid() || !faceFrag.isValid() || !lineVert.isValid() || !lineFrag.isValid() ||
        !pointVert.isValid() || !pointFrag.isValid() || !segmentVert.isValid()) {
        return false;
    }

    d.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                    kUniformFloats * sizeof(float)));
    d.corners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                   static_cast<quint32>(sizeof(kCorners))));
    if (!d.uniforms->create() || !d.corners->create()) {
        *error = QStringLiteral("could not create the mesh uniform buffers");
        return false;
    }
    d.bindings.reset(rhi->newShaderResourceBindings());
    d.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
        d.uniforms.get())});
    if (!d.bindings->create()) {
        *error = QStringLiteral("could not create the mesh resource bindings");
        return false;
    }

    // Faces and lines: position (binding 0) and color (binding 1).
    QRhiVertexInputLayout surface;
    surface.setBindings({{3 * sizeof(float)}, {sizeof(Rgba)}});
    surface.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0},
                           {1, 1, QRhiVertexInputAttribute::UNormByte4, 0}});

    // Points: corner per vertex, position and color per instance.
    QRhiVertexInputLayout instanced;
    instanced.setBindings(
        {{2 * sizeof(float)}, {sizeof(PointInstance), QRhiVertexInputBinding::PerInstance}});
    instanced.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                             {1, 1, QRhiVertexInputAttribute::Float3,
                              static_cast<quint32>(offsetof(PointInstance, position))},
                             {1, 2, QRhiVertexInputAttribute::UNormByte4,
                              static_cast<quint32>(offsetof(PointInstance, color))}});

    d.facePipeline.reset(d.makePipeline(faceVert, faceFrag, surface,
                                        QRhiGraphicsPipeline::Triangles, renderPass, sampleCount));
    // REQ-004-13: faces are pushed back so that their edges win the depth test.
    d.facePipeline->setDepthBias(1);
    d.facePipeline->setSlopeScaledDepthBias(1.0F);
    d.linePipeline.reset(d.makePipeline(lineVert, lineFrag, surface, QRhiGraphicsPipeline::Lines,
                                        renderPass, sampleCount));
    d.pointPipeline.reset(d.makePipeline(pointVert, pointFrag, instanced,
                                         QRhiGraphicsPipeline::Triangles, renderPass, sampleCount));

    // Thick segments: corner per vertex, both ends and color per instance.
    QRhiVertexInputLayout segments;
    segments.setBindings(
        {{2 * sizeof(float)}, {sizeof(SegmentInstance), QRhiVertexInputBinding::PerInstance}});
    segments.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                            {1, 1, QRhiVertexInputAttribute::Float3,
                             static_cast<quint32>(offsetof(SegmentInstance, from))},
                            {1, 2, QRhiVertexInputAttribute::Float3,
                             static_cast<quint32>(offsetof(SegmentInstance, to))},
                            {1, 3, QRhiVertexInputAttribute::UNormByte4,
                             static_cast<quint32>(offsetof(SegmentInstance, color))}});
    // REQ-004-26: outlines of the selected elements and selected edges, depth
    // tested so that the faces hide those behind them.
    d.outlinePipeline.reset(d.makePipeline(segmentVert, pointFrag, segments,
                                           QRhiGraphicsPipeline::Triangles, renderPass,
                                           sampleCount));
    d.outlinePipeline->setDepthWrite(false);

    if (!d.facePipeline->create() || !d.linePipeline->create() || !d.pointPipeline->create() ||
        !d.outlinePipeline->create()) {
        *error = QStringLiteral("could not create the mesh pipelines");
        return false;
    }
    d.uploaded = false;
    return true;
}

void MeshGpu::release()
{
    *m_impl = Impl{};
    m_hasScene = false;
}

bool MeshGpu::prepare(QRhiResourceUpdateBatch* updates, const MeshFrame& frame,
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

    const MeshScene* scene = frame.scene;
    m_hasScene = scene != nullptr;
    if (scene == nullptr) {
        d.scene = nullptr;
        return true;
    }

    // Positions: once per scene.
    const bool newScene = d.scene != scene || d.sceneVersion != frame.sceneVersion;
    if (newScene) {
        const auto triangles = scene->triangleVertices();
        const auto lines = scene->lineVertices();
        d.trianglePositions = d.staticBuffer(static_cast<quint32>(triangles.size_bytes()),
                                             triangles.data(), updates, error);
        d.linePositions =
            d.staticBuffer(static_cast<quint32>(lines.size_bytes()), lines.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.triangleRanges = {scene->triangles(2), scene->triangles(3)};
        d.lineRanges = {scene->lines(LineSet::Elements1d), scene->lines(LineSet::Edges2d),
                        scene->lines(LineSet::Edges3d)};
        d.scene = scene;
        d.sceneVersion = frame.sceneVersion;
    }

    // Colors and selected nodes: when the color mode or the selection changes.
    if (newScene || d.colorVersion != frame.colorVersion) {
        const std::vector<Rgba> elementColors = scene->elementColors(frame.display.color);
        const Selection* selection = frame.selection;
        const MeshScene* indexScene = frame.indexScene != nullptr ? frame.indexScene : scene;
        // Isolated (REQ-004-37): everything drawn is selected, so the elements
        // keep their colors and the selected components are highlighted.
        const bool isolated = frame.isolated;
        const auto colorOf = [&](core::ElementIndex element) {
            Rgba color = elementColors[static_cast<std::size_t>(element)];
            if (!isolated && selection != nullptr &&
                selection->contains(SelectionKind::Element, element)) {
                color |= kSelectedFlag;
            }
            return color;
        };

        // REQ-004-26: a solid triangle is highlighted when its element or its
        // face is selected.
        std::vector<Rgba> triangleColors;
        const auto triangleElements = scene->triangleElements();
        const auto triangleFaces = scene->triangleFaces();
        const Range solids = scene->triangles(3);
        triangleColors.reserve(triangleElements.size() * 3);
        for (std::size_t t = 0; t < triangleElements.size(); ++t) {
            Rgba color = colorOf(triangleElements[t]);
            const bool solid = t >= solids.first && t < solids.first + solids.count;
            if (!isolated && solid && selection != nullptr) {
                const std::uint32_t face = triangleFaces[t];
                // A selected 2D element lying on this face would be hidden by
                // it: the face shows it.
                const core::ElementIndex covering = scene->coveringElement(face);
                if (selection->contains(SelectionKind::Face, static_cast<int>(face)) ||
                    (covering >= 0 && selection->contains(SelectionKind::Element, covering))) {
                    color |= kSelectedFlag;
                }
            }
            triangleColors.insert(triangleColors.end(), 3, color);
        }
        std::vector<Rgba> lineColors;
        lineColors.reserve(scene->lineElements().size() * 2);
        const auto lineElements = scene->lineElements();
        for (std::size_t s = 0; s < lineElements.size(); ++s) {
            Rgba color = colorOf(lineElements[s]);
            if (s < d.lineRanges[0].count) {
                color |= kElementLineFlag;
            }
            lineColors.insert(lineColors.end(), 2, color);
        }
        std::vector<PointInstance> points;
        const auto positions = scene->nodePositions();
        for (const core::ElementIndex element : scene->pointElements()) {
            const auto node = scene->mesh().elementNodes(element)[0];
            points.push_back({.position = toArray(positions[static_cast<std::size_t>(node)]),
                              .color = colorOf(element)});
        }
        // Isolated (REQ-004-37): nothing is highlighted.
        std::vector<PointInstance> selectedNodes;
        std::vector<SegmentInstance> selectedEdges;
        std::vector<SegmentInstance> outlines;
        if (selection != nullptr && !isolated) {
            for (const int node : selection->items(SelectionKind::Node)) {
                selectedNodes.push_back(
                    {.position = toArray(positions[static_cast<std::size_t>(node)]),
                     .color = kSelectedFlag});
            }
            const auto segment = [&](std::vector<SegmentInstance>& into, core::NodeIndex a,
                                     core::NodeIndex b) {
                into.push_back({.from = toArray(positions[static_cast<std::size_t>(a)]),
                                .to = toArray(positions[static_cast<std::size_t>(b)]),
                                .color = kSelectedFlag});
            };
            for (const int e : selection->items(SelectionKind::Edge)) {
                const core::SurfaceEdge& edge = indexScene->edge(e);
                segment(selectedEdges, edge.a, edge.b);
            }
            // REQ-004-26: the edges of the selected elements, also those that
            // the line buffer gives to a neighbour; depth tested when drawn,
            // so only the visible ones show.
            if (selection->count(SelectionKind::Element) > 0) {
                const auto faceEdges = [&](const std::vector<core::SurfaceFace>& faces) {
                    for (const core::SurfaceFace& face : faces) {
                        if (!selection->contains(SelectionKind::Element, face.element)) {
                            continue;
                        }
                        for (std::size_t v = 0; v < face.nodeCount; ++v) {
                            segment(outlines, face.nodes.at(v),
                                    face.nodes.at((v + 1) % face.nodeCount));
                        }
                    }
                };
                faceEdges(scene->surface().faces2d);
                faceEdges(scene->surface().faces3d);
            }
        }

        d.triangleColors =
            d.staticBuffer(bytes(triangleColors), triangleColors.data(), updates, error);
        d.lineColors = d.staticBuffer(bytes(lineColors), lineColors.data(), updates, error);
        d.points = d.staticBuffer(bytes(points), points.data(), updates, error);
        d.selectedNodes =
            d.staticBuffer(bytes(selectedNodes), selectedNodes.data(), updates, error);
        d.selectedEdges =
            d.staticBuffer(bytes(selectedEdges), selectedEdges.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.pointCount = static_cast<quint32>(points.size());
        d.selectedNodeCount = static_cast<quint32>(selectedNodes.size());
        d.selectedEdgeCount = static_cast<quint32>(selectedEdges.size());
        d.outlines = d.staticBuffer(bytes(outlines), outlines.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.outlineCount = static_cast<quint32>(outlines.size());
        d.colorVersion = frame.colorVersion;
    }

    // REQ-004-34: the nodes of the visible surface, when shown.
    if (newScene || d.nodeVersion != frame.nodeVersion) {
        std::vector<core::NodeIndex> nodes;
        if (frame.display.showNodes) {
            for (int dimension = 0; dimension < 4; ++dimension) {
                if (frame.display.isVisible(dimension)) {
                    const auto surface = scene->surfaceNodes(dimension);
                    nodes.insert(nodes.end(), surface.begin(), surface.end());
                }
            }
            std::ranges::sort(nodes);
            const auto duplicates = std::ranges::unique(nodes);
            nodes.erase(duplicates.begin(), duplicates.end());
        }
        const auto positions = scene->nodePositions();
        std::vector<PointInstance> shown;
        shown.reserve(nodes.size());
        for (const core::NodeIndex node : nodes) {
            shown.push_back({.position = toArray(positions[static_cast<std::size_t>(node)]),
                             .color = kNodeColor});
        }
        d.shownNodes = d.staticBuffer(bytes(shown), shown.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.shownNodeCount = static_cast<quint32>(shown.size());
        d.nodeVersion = frame.nodeVersion;
    }

    // Uniforms, every frame. Positions are relative to the scene origin; the
    // offset to the camera target is computed in double (REQ-004-18).
    const Mat4 view = camera.viewMatrixRelativeToTarget();
    const QMatrix4x4 viewProjection =
        d.rhi->clipSpaceCorrMatrix() * toQt(toOpenGlDepth(camera.projectionMatrix()) * view);
    const Eigen::Vector3d offset = scene->origin() - camera.target();

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
    data.at(i++) = frame.display.mode == DisplayMode::Wireframe ? 1.0F : 0.0F;
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

void MeshGpu::draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport,
                   const DisplayOptions& display) const
{
    const Impl& d = *m_impl;
    if (d.scene == nullptr) {
        return;
    }

    // REQ-004-11: faces except in wireframe.
    if (display.mode != DisplayMode::Wireframe && d.trianglePositions && d.triangleColors) {
        commandBuffer->setGraphicsPipeline(d.facePipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.trianglePositions.get(), 0},
            {d.triangleColors.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        for (std::size_t r = 0; r < d.triangleRanges.size(); ++r) {
            const Range range = d.triangleRanges.at(r);
            if (range.count > 0 && display.isVisible(static_cast<int>(r) + 2)) {
                commandBuffer->draw(range.count * 3, 1, range.first * 3);
            }
        }
    }

    // 1D elements always; edges except in the shaded mode.
    if (d.linePositions && d.lineColors) {
        commandBuffer->setGraphicsPipeline(d.linePipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.linePositions.get(), 0},
            {d.lineColors.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        const std::array<int, 3> dimensions = {1, 2, 3};
        for (std::size_t r = 0; r < d.lineRanges.size(); ++r) {
            const Range range = d.lineRanges.at(r);
            const bool isEdge = r > 0;
            if (range.count == 0 || !display.isVisible(dimensions.at(r)) ||
                (isEdge && display.mode == DisplayMode::Shaded)) {
                continue;
            }
            commandBuffer->draw(range.count * 2, 1, range.first * 2);
        }
    }

    if (d.points && d.pointCount > 0 && display.isVisible(0)) {
        commandBuffer->setGraphicsPipeline(d.pointPipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.corners.get(), 0},
            {d.points.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(6, d.pointCount);
    }

    if (d.outlines && d.outlineCount > 0) {
        commandBuffer->setGraphicsPipeline(d.outlinePipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.corners.get(), 0},
            {d.outlines.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(6, d.outlineCount);
    }
    if (d.selectedEdges && d.selectedEdgeCount > 0) {
        commandBuffer->setGraphicsPipeline(d.outlinePipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.corners.get(), 0},
            {d.selectedEdges.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(6, d.selectedEdgeCount);
    }

    // REQ-004-35: depth tested, so the faces hide the nodes behind them.
    if (display.showNodes && d.shownNodes && d.shownNodeCount > 0) {
        commandBuffer->setGraphicsPipeline(d.pointPipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.corners.get(), 0},
            {d.shownNodes.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(6, d.shownNodeCount);
    }
    // REQ-004-26: selected nodes, depth tested; after the shown nodes so that
    // they come out on top of them.
    if (d.selectedNodes && d.selectedNodeCount > 0) {
        commandBuffer->setGraphicsPipeline(d.pointPipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources();
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.corners.get(), 0},
            {d.selectedNodes.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(6, d.selectedNodeCount);
    }
}

} // namespace fem::render
