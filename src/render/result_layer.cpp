#include "result_layer.hpp"

#include "rhi_util.hpp"

#include <fem/render/colormap.hpp>

#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

namespace fem::render {

namespace {

using detail::copyMatrix;
using detail::kMatrixFloats;
using detail::kVectorFloats;
using detail::loadShader;
using detail::toOpenGlDepth;
using detail::toQt;

constexpr int kUniformFloats = kMatrixFloats + 7 * kVectorFloats;
constexpr float kDeformedWidthPx = 3.0F;
constexpr float kOriginalWidthPx = 1.5F;
constexpr std::array<float, 4> kNeutralColor = {0.55F, 0.57F, 0.62F, 1.0F};
constexpr std::array<float, 4> kHighlightColor = {1.0F, 0.55F, 0.0F, 1.0F};
constexpr std::uint32_t kSelectedFlag = 1U;

// Two triangles covering the square [-1, 1]^2.
constexpr std::array<float, 12> kCorners = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};

// Per segment: both ends, where they are and how they move (48 bytes).
struct SegmentGeometry
{
    std::array<float, 3> fromPosition;
    std::array<float, 3> fromDisplacement;
    std::array<float, 3> toPosition;
    std::array<float, 3> toDisplacement;
};
static_assert(sizeof(SegmentGeometry) + 2 * sizeof(float) + sizeof(std::uint32_t) ==
              kSegmentGpuBytes);

struct NodeInstance
{
    std::array<float, 3> position;
    std::array<float, 3> displacement;
};

std::array<float, 3> toArray(const Eigen::Vector3f& p)
{
    return {p.x(), p.y(), p.z()};
}

std::array<float, 4> toColor(Rgba c)
{
    const auto channel = [](Rgba v) { return static_cast<float>(v & 0xFFU) / 255.0F; };
    return {channel(c), channel(c >> 8U), channel(c >> 16U), 1.0F};
}

} // namespace

struct ResultLayer::Impl
{
    QRhi* rhi = nullptr;
    // One uniform block per draw of the segments: the original shape and
    // the colored one (the nodes use the colored one).
    std::array<std::unique_ptr<QRhiBuffer>, 2> uniforms;
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, 2> bindings;
    std::unique_ptr<QRhiTexture> colormap;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiGraphicsPipeline> linePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> pointPipeline;
    std::unique_ptr<QRhiBuffer> corners;

    std::unique_ptr<QRhiBuffer> geometry;
    std::unique_ptr<QRhiBuffer> values;
    std::unique_ptr<QRhiBuffer> flags;
    std::unique_ptr<QRhiBuffer> nodes;
    quint32 segmentCount = 0;
    quint32 nodeCount = 0;

    const ResultScene* scene = nullptr;
    std::uint64_t sceneVersion = 0;
    std::uint64_t valueVersion = 0;
    std::uint64_t selectionVersion = 0;
    bool highlight = false;
    bool staticUploaded = false;
    bool drawOriginal = false;
    Uploads uploads;

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
            *error = QStringLiteral("could not create a result buffer of %1 bytes").arg(size);
            return {};
        }
        updates->uploadStaticBuffer(buffer.get(), data);
        return buffer;
    }

    [[nodiscard]] QRhiGraphicsPipeline* makePipeline(const QShader& vertex, const QShader& fragment,
                                                     const QRhiVertexInputLayout& layout,
                                                     QRhiRenderPassDescriptor* renderPass,
                                                     int sampleCount) const
    {
        QRhiGraphicsPipeline* pipeline = rhi->newGraphicsPipeline();
        pipeline->setShaderStages(
            {{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
        pipeline->setVertexInputLayout(layout);
        pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
        pipeline->setCullMode(QRhiGraphicsPipeline::None);
        pipeline->setDepthTest(true);
        pipeline->setDepthWrite(true);
        pipeline->setDepthOp(QRhiGraphicsPipeline::LessOrEqual);
        pipeline->setSampleCount(sampleCount);
        // Compatible with both bindings: the same layout.
        pipeline->setShaderResourceBindings(bindings[1].get());
        pipeline->setRenderPassDescriptor(renderPass);
        return pipeline;
    }

    void uploadFlags(const ResultFrame& frame, QRhiResourceUpdateBatch* updates, QString* error)
    {
        const auto elements = scene->segmentElements();
        const Selection* selection = frame.highlight ? frame.selection : nullptr;
        std::vector<std::uint32_t> data(elements.size(), 0U);
        std::vector<NodeInstance> selectedNodes;
        if (selection != nullptr) {
            for (std::size_t s = 0; s < elements.size(); ++s) {
                if (selection->contains(SelectionKind::Element, elements[s])) {
                    data[s] = kSelectedFlag;
                }
            }
            const auto samples = scene->nodeSamples();
            for (const int node : selection->items(SelectionKind::Node)) {
                const auto n = static_cast<std::size_t>(node);
                if (n < samples.size() && samples[n] != kNoSample) {
                    selectedNodes.push_back(
                        {.position = toArray(scene->positions()[samples[n]]),
                         .displacement = toArray(scene->displacements()[samples[n]])});
                }
            }
        }
        flags = staticBuffer(static_cast<quint32>(data.size() * sizeof(std::uint32_t)), data.data(),
                             updates, error);
        nodes = staticBuffer(static_cast<quint32>(selectedNodes.size() * sizeof(NodeInstance)),
                             selectedNodes.data(), updates, error);
        nodeCount = static_cast<quint32>(selectedNodes.size());
        ++uploads.flags;
    }
};

ResultLayer::ResultLayer()
    : m_impl(std::make_unique<Impl>())
{}

ResultLayer::~ResultLayer() = default;

bool ResultLayer::create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                         QString* error)
{
    release();
    Impl& d = *m_impl;
    d.rhi = rhi;

    const QShader lineVert = loadShader(QStringLiteral("result_line.vert"), error);
    const QShader lineFrag = loadShader(QStringLiteral("result_line.frag"), error);
    const QShader pointVert = loadShader(QStringLiteral("result_point.vert"), error);
    const QShader pointFrag = loadShader(QStringLiteral("result_point.frag"), error);
    if (!lineVert.isValid() || !lineFrag.isValid() || !pointVert.isValid() ||
        !pointFrag.isValid()) {
        return false;
    }

    const QImage scale = colormapImage();
    d.colormap.reset(rhi->newTexture(QRhiTexture::RGBA8, scale.size()));
    d.sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                    QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    d.corners.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                   static_cast<quint32>(sizeof(kCorners))));
    if (!d.colormap->create() || !d.sampler->create() || !d.corners->create()) {
        *error = QStringLiteral("could not create the result resources");
        return false;
    }
    for (std::size_t i = 0; i < 2; ++i) {
        d.uniforms.at(i).reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                              kUniformFloats * sizeof(float)));
        if (!d.uniforms.at(i)->create()) {
            *error = QStringLiteral("could not create the result uniform buffers");
            return false;
        }
        d.bindings.at(i).reset(rhi->newShaderResourceBindings());
        d.bindings.at(i)->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,
                                                     QRhiShaderResourceBinding::VertexStage |
                                                         QRhiShaderResourceBinding::FragmentStage,
                                                     d.uniforms.at(i).get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                      d.colormap.get(), d.sampler.get()),
        });
        if (!d.bindings.at(i)->create()) {
            *error = QStringLiteral("could not create the result resource bindings");
            return false;
        }
    }

    // Segments: corner per vertex (binding 0); per instance, the geometry
    // (1), the values (2) and the flags (3).
    QRhiVertexInputLayout lines;
    lines.setBindings({{2 * sizeof(float)},
                       {sizeof(SegmentGeometry), QRhiVertexInputBinding::PerInstance},
                       {2 * sizeof(float), QRhiVertexInputBinding::PerInstance},
                       {sizeof(std::uint32_t), QRhiVertexInputBinding::PerInstance}});
    lines.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                         {1, 1, QRhiVertexInputAttribute::Float3,
                          static_cast<quint32>(offsetof(SegmentGeometry, fromPosition))},
                         {1, 2, QRhiVertexInputAttribute::Float3,
                          static_cast<quint32>(offsetof(SegmentGeometry, fromDisplacement))},
                         {1, 3, QRhiVertexInputAttribute::Float3,
                          static_cast<quint32>(offsetof(SegmentGeometry, toPosition))},
                         {1, 4, QRhiVertexInputAttribute::Float3,
                          static_cast<quint32>(offsetof(SegmentGeometry, toDisplacement))},
                         {2, 5, QRhiVertexInputAttribute::Float2, 0},
                         {3, 6, QRhiVertexInputAttribute::UNormByte4, 0}});
    QRhiVertexInputLayout points;
    points.setBindings(
        {{2 * sizeof(float)}, {sizeof(NodeInstance), QRhiVertexInputBinding::PerInstance}});
    points.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0},
                          {1, 1, QRhiVertexInputAttribute::Float3,
                           static_cast<quint32>(offsetof(NodeInstance, position))},
                          {1, 2, QRhiVertexInputAttribute::Float3,
                           static_cast<quint32>(offsetof(NodeInstance, displacement))}});

    d.linePipeline.reset(d.makePipeline(lineVert, lineFrag, lines, renderPass, sampleCount));
    d.pointPipeline.reset(d.makePipeline(pointVert, pointFrag, points, renderPass, sampleCount));
    if (!d.linePipeline->create() || !d.pointPipeline->create()) {
        *error = QStringLiteral("could not create the result pipelines");
        return false;
    }
    d.staticUploaded = false;
    return true;
}

void ResultLayer::release()
{
    *m_impl = Impl{};
}

bool ResultLayer::prepare(QRhiResourceUpdateBatch* updates, const ResultFrame& frame,
                          const Camera& camera, QSize pixelSize, double devicePixelRatio,
                          QString* error)
{
    Impl& d = *m_impl;
    if (d.rhi == nullptr) {
        return false;
    }
    if (!d.staticUploaded) {
        updates->uploadStaticBuffer(d.corners.get(), kCorners.data());
        updates->uploadTexture(d.colormap.get(), colormapImage());
        d.staticUploaded = true;
    }
    const ResultScene* scene = frame.scene;
    if (scene == nullptr) {
        d.scene = nullptr;
        return true;
    }

    // The geometry: once per scene (REQ-012-08).
    const bool newScene = d.scene != scene || d.sceneVersion != frame.sceneVersion;
    if (newScene) {
        const auto starts = scene->segmentStarts();
        const auto positions = scene->positions();
        const auto displacements = scene->displacements();
        std::vector<SegmentGeometry> geometry;
        geometry.reserve(starts.size());
        for (const std::uint32_t a : starts) {
            geometry.push_back({.fromPosition = toArray(positions[a]),
                                .fromDisplacement = toArray(displacements[a]),
                                .toPosition = toArray(positions[a + 1]),
                                .toDisplacement = toArray(displacements[a + 1])});
        }
        d.geometry = d.staticBuffer(static_cast<quint32>(geometry.size() * sizeof(SegmentGeometry)),
                                    geometry.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.segmentCount = static_cast<quint32>(starts.size());
        d.scene = scene;
        d.sceneVersion = frame.sceneVersion;
        ++d.uploads.geometry;
    }

    // The values: with the quantity (REQ-012-13); NaN without them.
    if (newScene || d.valueVersion != frame.valueVersion) {
        std::vector<float> values(2 * static_cast<std::size_t>(d.segmentCount),
                                  std::numeric_limits<float>::quiet_NaN());
        if (frame.values != nullptr && frame.values->size() == values.size()) {
            values = *frame.values;
        }
        d.values = d.staticBuffer(static_cast<quint32>(values.size() * sizeof(float)),
                                  values.data(), updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.valueVersion = frame.valueVersion;
        ++d.uploads.values;
    }

    // The selection (REQ-011-15), on the mesh of the result only.
    if (newScene || d.selectionVersion != frame.selectionVersion ||
        d.highlight != frame.highlight) {
        d.uploadFlags(frame, updates, error);
        if (!error->isEmpty()) {
            return false;
        }
        d.selectionVersion = frame.selectionVersion;
        d.highlight = frame.highlight;
    }

    // Uniforms, every frame: relative to the origin of the result, the
    // offset to the camera target in double (REQ-004-18).
    const ResultDisplay& display = frame.display;
    const Mat4 view = camera.viewMatrixRelativeToTarget();
    const QMatrix4x4 viewProjection =
        d.rhi->clipSpaceCorrMatrix() * toQt(toOpenGlDepth(camera.projectionMatrix()) * view);
    const Eigen::Vector3d offset = scene->origin() - camera.target();
    const double colored = display.deformed ? frame.scale : 0.0;
    d.drawOriginal = display.original && display.deformed;
    const std::array<float, 4> plain = toColor(noValueColor());
    for (std::size_t pass = 0; pass < 2; ++pass) {
        const bool original = pass == 0;
        std::array<float, kUniformFloats> data{};
        copyMatrix(viewProjection, data.data());
        std::size_t i = kMatrixFloats;
        const auto put = [&](std::initializer_list<float> values) {
            for (const float v : values) {
                data.at(i++) = v;
            }
        };
        put({static_cast<float>(offset.x()), static_cast<float>(offset.y()),
             static_cast<float>(offset.z()), 0.0F});
        put({static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height()),
             static_cast<float>(devicePixelRatio), 0.0F});
        put({original ? 0.0F : static_cast<float>(colored),
             original ? kOriginalWidthPx : kDeformedWidthPx, original ? 1.0F : 0.0F, 0.0F});
        put({static_cast<float>(display.min), static_cast<float>(display.max),
             static_cast<float>(std::max(display.bands, 0)), display.colored ? 1.0F : 0.0F});
        for (const auto& color : {kNeutralColor, plain, kHighlightColor}) {
            put({color[0], color[1], color[2], color[3]});
        }
        updates->updateDynamicBuffer(d.uniforms.at(pass).get(), 0,
                                     static_cast<quint32>(sizeof(data)), data.data());
    }
    return true;
}

void ResultLayer::draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const
{
    const Impl& d = *m_impl;
    if (d.scene == nullptr || !d.geometry || !d.values || !d.flags || d.segmentCount == 0) {
        return;
    }
    const std::array<QRhiCommandBuffer::VertexInput, 4> inputs = {{
        {d.corners.get(), 0},
        {d.geometry.get(), 0},
        {d.values.get(), 0},
        {d.flags.get(), 0},
    }};
    for (std::size_t pass = d.drawOriginal ? 0 : 1; pass < 2; ++pass) {
        commandBuffer->setGraphicsPipeline(d.linePipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources(d.bindings.at(pass).get());
        commandBuffer->setVertexInput(0, 4, inputs.data());
        commandBuffer->draw(6, d.segmentCount);
    }
    if (d.nodes && d.nodeCount > 0) {
        commandBuffer->setGraphicsPipeline(d.pointPipeline.get());
        commandBuffer->setViewport(viewport);
        commandBuffer->setShaderResources(d.bindings.at(1).get());
        const std::array<QRhiCommandBuffer::VertexInput, 2> nodeInputs = {{
            {d.corners.get(), 0},
            {d.nodes.get(), 0},
        }};
        commandBuffer->setVertexInput(0, 2, nodeInputs.data());
        commandBuffer->draw(6, d.nodeCount);
    }
}

bool ResultLayer::hasScene() const noexcept
{
    return m_impl->scene != nullptr;
}

ResultLayer::Uploads ResultLayer::uploads() const noexcept
{
    return m_impl->uploads;
}

} // namespace fem::render
