#include "symbol_layer.hpp"

#include "rhi_util.hpp"

#include <rhi/qrhi.h>

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fem::render {

SupportShape supportShape(std::uint8_t held, bool planar)
{
    const auto isHeld = [held](int dof) {
        return (held & (1U << static_cast<unsigned>(dof))) != 0;
    };
    // U, V, W, RX, RY, RZ as in core::DofType.
    const std::array<int, 3> translations{0, 1, 2};
    const std::array<int, 3> rotations{3, 4, 5};
    std::vector<int> moving;
    std::vector<int> turning;
    for (const int t : translations) {
        if ((!planar || t != 2) && isHeld(t)) {
            moving.push_back(t);
        }
    }
    for (const int r : rotations) {
        if ((!planar || r == 5) && isHeld(r)) {
            turning.push_back(r);
        }
    }
    const std::size_t allMoving = planar ? 2 : 3;
    const std::size_t allTurning = planar ? 1 : 3;
    SupportShape shape;
    if (moving.size() == allMoving && turning.size() == allTurning) {
        shape.glyph = SupportGlyph::Fixed;
    } else if (moving.size() == allMoving && turning.empty()) {
        shape.glyph = SupportGlyph::Pinned;
    } else if (moving.size() == 1 && turning.empty()) {
        shape.glyph = SupportGlyph::Roller;
        shape.up = Eigen::Vector3d::Unit(moving.front());
    }
    return shape;
}

namespace detail {

namespace {

constexpr Rgba rgb(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept
{
    return r | (g << 8U) | (b << 16U);
}

constexpr Rgba kOpaque = 0xFFU << 24U;

// Sizes in logical pixels.
constexpr float kSupportSize = 28.0F;
constexpr float kForceSize = 56.0F;
constexpr float kMomentSize = 40.0F;
constexpr float kDistributedSize = 44.0F;
constexpr float kAxesSize = 44.0F;
constexpr int kDistributedArrows = 8;

constexpr Rgba kSupportColor = rgb(60, 200, 120);
constexpr Rgba kForceColor = rgb(235, 80, 60);
constexpr Rgba kMomentColor = rgb(225, 90, 210);
constexpr Rgba kDistributedColor = rgb(245, 160, 50);

struct Builder
{
    std::vector<MoldVertex> vertices;

    void line(float x0, float y0, float z0, float x1, float y1, float z1, Rgba color = 0)
    {
        vertices.push_back({.position = {x0, y0, z0, 0.0F}, .color = color});
        vertices.push_back({.position = {x1, y1, z1, 0.0F}, .color = color});
    }
    // Hatches below a ground line at y, from x0 to x1.
    void ground(float y, float x0, float x1)
    {
        line(x0, y, 0, x1, y, 0);
        constexpr int kHatches = 5;
        for (int k = 0; k < kHatches; ++k) {
            const float x = x0 + (x1 - x0) * (static_cast<float>(k) + 0.5F) / kHatches;
            line(x, y, 0, x - 0.15F, y - 0.2F, 0);
        }
    }
    void triangle(float height, float half)
    {
        line(0, 0, 0, -half, -height, 0);
        line(-half, -height, 0, half, -height, 0);
        line(half, -height, 0, 0, 0, 0);
    }
};

std::vector<MoldVertex> buildMold(Mold mold)
{
    Builder b;
    switch (mold) {
    case Mold::Fixed:
        b.line(-0.6F, 0, 0, 0.6F, 0, 0);
        b.ground(0.0F, -0.6F, 0.6F);
        break;
    case Mold::Pinned:
        b.triangle(0.5F, 0.35F);
        b.ground(-0.5F, -0.55F, 0.55F);
        break;
    case Mold::Roller:
        b.triangle(0.5F, 0.35F);
        b.line(-0.5F, -0.6F, 0, 0.5F, -0.6F, 0);
        b.ground(-0.72F, -0.55F, 0.55F);
        break;
    case Mold::Generic:
        // A diamond in two planes about the node.
        b.line(0, 0.3F, 0, 0.3F, 0, 0);
        b.line(0.3F, 0, 0, 0, -0.3F, 0);
        b.line(0, -0.3F, 0, -0.3F, 0, 0);
        b.line(-0.3F, 0, 0, 0, 0.3F, 0);
        b.line(0, 0, 0.3F, 0.3F, 0, 0);
        b.line(0.3F, 0, 0, 0, 0, -0.3F);
        b.line(0, 0, -0.3F, -0.3F, 0, 0);
        b.line(-0.3F, 0, 0, 0, 0, 0.3F);
        break;
    case Mold::Arrow:
        // Along +x, the tip at the origin.
        b.line(-1.0F, 0, 0, 0, 0, 0);
        b.line(0, 0, 0, -0.22F, 0.09F, 0);
        b.line(0, 0, 0, -0.22F, -0.09F, 0);
        b.line(0, 0, 0, -0.22F, 0, 0.09F);
        b.line(0, 0, 0, -0.22F, 0, -0.09F);
        break;
    case Mold::Arc: {
        // Three quarters of a circle about +x, counterclockwise seen from
        // +x, with its head at the end.
        constexpr int kSegments = 18;
        constexpr float kRadius = 0.5F;
        constexpr float kSweep = 1.5F * std::numbers::pi_v<float>;
        const auto at = [&](int k) {
            const float a = kSweep * static_cast<float>(k) / kSegments;
            return std::array<float, 2>{kRadius * std::cos(a), kRadius * std::sin(a)};
        };
        for (int k = 0; k < kSegments; ++k) {
            const auto p = at(k);
            const auto q = at(k + 1);
            b.line(0, p[0], p[1], 0, q[0], q[1]);
        }
        // The end is at angle 3pi/2: (y, z) = (0, -r), moving toward +y.
        b.line(0, 0, -kRadius, 0, -0.15F, -kRadius + 0.12F);
        b.line(0, 0, -kRadius, 0, -0.15F, -kRadius - 0.12F);
        break;
    }
    case Mold::Triad:
        b.line(0, 0, 0, 1, 0, 0, rgb(230, 60, 60) | kOpaque);
        b.line(0, 0, 0, 0, 1, 0, rgb(60, 200, 60) | kOpaque);
        b.line(0, 0, 0, 0, 0, 1, rgb(70, 120, 240) | kOpaque);
        break;
    case Mold::Link:
        // From the tail of one arrow (x) to the tail of the next: y is the
        // step along the bar in the world, z the tail of the next arrow.
        b.vertices.push_back({.position = {1, 0, 0, 0}, .color = 0});
        b.vertices.push_back({.position = {0, 1, 1, 1}, .color = 0});
        break;
    }
    return std::move(b.vertices);
}

std::array<float, 3> toFloat(const Eigen::Vector3d& v)
{
    const Eigen::Vector3f f = v.cast<float>();
    return {f.x(), f.y(), f.z()};
}

// A right-handed basis whose x is along `x` (any length kept).
Eigen::Matrix3d basisAlong(const Eigen::Vector3d& x)
{
    const Eigen::Vector3d u = x.normalized();
    const Eigen::Vector3d ref =
        std::abs(u.z()) < 0.9 ? Eigen::Vector3d::UnitZ() : Eigen::Vector3d::UnitX();
    const Eigen::Vector3d y = ref.cross(u).normalized();
    Eigen::Matrix3d m;
    m.col(0) = x;
    m.col(1) = y;
    m.col(2) = u.cross(y);
    return m;
}

// A right-handed basis whose y is `up`.
Eigen::Matrix3d basisUp(const Eigen::Vector3d& up)
{
    const Eigen::Vector3d u = up.normalized();
    const Eigen::Vector3d ref =
        std::abs(u.z()) < 0.9 ? Eigen::Vector3d::UnitZ() : Eigen::Vector3d::UnitX();
    const Eigen::Vector3d x = u.cross(ref).normalized();
    Eigen::Matrix3d m;
    m.col(0) = x;
    m.col(1) = u;
    m.col(2) = x.cross(u);
    return m;
}

SymbolInstance instance(const Eigen::Vector3d& at, const Eigen::Matrix3d& basis, float size,
                        Rgba color)
{
    return {.at = toFloat(at),
            .x = toFloat(basis.col(0)),
            .y = toFloat(basis.col(1)),
            .z = toFloat(basis.col(2)),
            .size = size,
            .color = color};
}

std::size_t index(Mold mold)
{
    return static_cast<std::size_t>(mold);
}

} // namespace

const std::vector<MoldVertex>& moldVertices(Mold mold)
{
    static const std::array<std::vector<MoldVertex>, kMoldCount> kMolds = [] {
        std::array<std::vector<MoldVertex>, kMoldCount> molds;
        for (std::size_t m = 0; m < kMoldCount; ++m) {
            molds.at(m) = buildMold(static_cast<Mold>(m));
        }
        return molds;
    }();
    return kMolds.at(index(mold));
}

SymbolInstances instancesOf(const SymbolSet& set, const Eigen::Vector3d& origin)
{
    SymbolInstances out;
    for (const SupportSymbol& s : set.supports) {
        const SupportShape shape = supportShape(s.held, s.planar);
        Mold mold = Mold::Generic;
        switch (shape.glyph) {
        case SupportGlyph::Fixed:
            mold = Mold::Fixed;
            break;
        case SupportGlyph::Pinned:
            mold = Mold::Pinned;
            break;
        case SupportGlyph::Roller:
            mold = Mold::Roller;
            break;
        case SupportGlyph::Generic:
            break;
        }
        out.at(index(mold))
            .push_back(instance(s.at - origin, basisUp(shape.up), kSupportSize, kSupportColor));
    }
    for (const ArrowSymbol& f : set.forces) {
        if (f.vector.norm() > 0.0) {
            out.at(index(Mold::Arrow))
                .push_back(instance(f.at - origin, basisAlong(f.vector.normalized()), kForceSize,
                                    kForceColor));
        }
    }
    for (const MomentSymbol& m : set.moments) {
        if (m.axis.norm() > 0.0) {
            out.at(index(Mold::Arc))
                .push_back(instance(m.at - origin, basisAlong(m.axis.normalized()), kMomentSize,
                                    kMomentColor));
        }
    }
    for (const ArrowSymbol& f : set.reactionForces) {
        if (f.vector.norm() > 0.0) {
            out.at(index(Mold::Arrow))
                .push_back(instance(f.at - origin, basisAlong(f.vector.normalized()), kForceSize,
                                    kReactionColor));
        }
    }
    for (const MomentSymbol& m : set.reactionMoments) {
        if (m.axis.norm() > 0.0) {
            out.at(index(Mold::Arc))
                .push_back(instance(m.at - origin, basisAlong(m.axis.normalized()), kMomentSize,
                                    kReactionColor));
        }
    }
    // Distributed: the arrows as long as their value over the largest.
    double largest = 0.0;
    for (const DistributedSymbol& d : set.distributed) {
        largest = std::max({largest, d.startValue.norm(), d.endValue.norm()});
    }
    for (const DistributedSymbol& d : set.distributed) {
        if (largest <= 0.0) {
            break;
        }
        struct Tail
        {
            Eigen::Vector3d at;
            Eigen::Vector3d tail; // from `at`, in units of the size
        };
        std::vector<Tail> tails;
        for (int k = 0; k < kDistributedArrows; ++k) {
            const double t = static_cast<double>(k) / (kDistributedArrows - 1);
            const Eigen::Vector3d at = d.from + t * (d.to - d.from);
            const Eigen::Vector3d value = d.startValue + t * (d.endValue - d.startValue);
            const Eigen::Vector3d scaled = value / largest;
            if (scaled.norm() > 1e-9) {
                out.at(index(Mold::Arrow))
                    .push_back(instance(at - origin, basisAlong(scaled), kDistributedSize,
                                        kDistributedColor));
            }
            tails.push_back({.at = at, .tail = -scaled});
        }
        for (std::size_t k = 0; k + 1 < tails.size(); ++k) {
            Eigen::Matrix3d link;
            link.col(0) = tails[k].tail;
            link.col(1) = tails[k + 1].at - tails[k].at;
            link.col(2) = tails[k + 1].tail;
            out.at(index(Mold::Link))
                .push_back(
                    instance(tails[k].at - origin, link, kDistributedSize, kDistributedColor));
        }
    }
    for (const AxesSymbol& a : set.axes) {
        out.at(index(Mold::Triad))
            .push_back(instance(a.at - origin, a.axes, kAxesSize, kSupportColor));
    }
    return out;
}

} // namespace detail

namespace {

using detail::kMatrixFloats;
using detail::kMoldCount;
using detail::kVectorFloats;
using detail::MoldVertex;
using detail::SymbolInstance;

constexpr int kUniformFloats = kMatrixFloats + 2 * kVectorFloats;

} // namespace

struct SymbolLayer::Impl
{
    QRhi* rhi = nullptr;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiBuffer> molds;
    std::unique_ptr<QRhiBuffer> instances;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::array<quint32, kMoldCount> moldFirst{};
    std::array<quint32, kMoldCount> moldCount{};
    std::array<quint32, kMoldCount> instanceFirst{};
    std::array<int, kMoldCount> instanceCount{};
    std::vector<MoldVertex> moldData;
    bool uploaded = false;
    const SymbolSet* set = nullptr;
    std::uint64_t version = 0;
    bool visible = true;
    Eigen::Vector3d origin = Eigen::Vector3d::Zero();
};

SymbolLayer::SymbolLayer()
    : m_impl(std::make_unique<Impl>())
{}

SymbolLayer::~SymbolLayer() = default;

bool SymbolLayer::create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                         QString* error)
{
    release();
    Impl& d = *m_impl;
    d.rhi = rhi;
    const QShader vert = detail::loadShader(QStringLiteral("symbol.vert"), error);
    const QShader frag = detail::loadShader(QStringLiteral("symbol.frag"), error);
    if (!vert.isValid() || !frag.isValid()) {
        return false;
    }
    // Every mold in one buffer.
    for (std::size_t m = 0; m < kMoldCount; ++m) {
        const auto& vertices = detail::moldVertices(static_cast<detail::Mold>(m));
        d.moldFirst.at(m) = static_cast<quint32>(d.moldData.size());
        d.moldCount.at(m) = static_cast<quint32>(vertices.size());
        d.moldData.insert(d.moldData.end(), vertices.begin(), vertices.end());
    }
    d.uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
                                    kUniformFloats * sizeof(float)));
    d.molds.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                 static_cast<quint32>(d.moldData.size() * sizeof(MoldVertex))));
    if (!d.uniforms->create() || !d.molds->create()) {
        *error = QStringLiteral("could not create the symbol buffers");
        return false;
    }
    d.bindings.reset(rhi->newShaderResourceBindings());
    d.bindings->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, d.uniforms.get())});
    if (!d.bindings->create()) {
        *error = QStringLiteral("could not create the symbol resource bindings");
        return false;
    }
    QRhiVertexInputLayout layout;
    layout.setBindings(
        {{sizeof(MoldVertex)}, {sizeof(SymbolInstance), QRhiVertexInputBinding::PerInstance}});
    const auto at = [](std::size_t offset) { return static_cast<quint32>(offset); };
    layout.setAttributes(
        {{0, 0, QRhiVertexInputAttribute::Float4, at(offsetof(MoldVertex, position))},
         {0, 1, QRhiVertexInputAttribute::UNormByte4, at(offsetof(MoldVertex, color))},
         {1, 2, QRhiVertexInputAttribute::Float3, at(offsetof(SymbolInstance, at))},
         {1, 3, QRhiVertexInputAttribute::Float3, at(offsetof(SymbolInstance, x))},
         {1, 4, QRhiVertexInputAttribute::Float3, at(offsetof(SymbolInstance, y))},
         {1, 5, QRhiVertexInputAttribute::Float3, at(offsetof(SymbolInstance, z))},
         {1, 6, QRhiVertexInputAttribute::Float, at(offsetof(SymbolInstance, size))},
         {1, 7, QRhiVertexInputAttribute::UNormByte4, at(offsetof(SymbolInstance, color))}});
    d.pipeline.reset(rhi->newGraphicsPipeline());
    d.pipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, vert}, {QRhiShaderStage::Fragment, frag}});
    d.pipeline->setVertexInputLayout(layout);
    d.pipeline->setTopology(QRhiGraphicsPipeline::Lines);
    d.pipeline->setCullMode(QRhiGraphicsPipeline::None);
    d.pipeline->setDepthTest(false);
    d.pipeline->setDepthWrite(false);
    d.pipeline->setSampleCount(sampleCount);
    d.pipeline->setShaderResourceBindings(d.bindings.get());
    d.pipeline->setRenderPassDescriptor(renderPass);
    if (!d.pipeline->create()) {
        *error = QStringLiteral("could not create the symbol pipeline");
        return false;
    }
    return true;
}

void SymbolLayer::release()
{
    *m_impl = Impl{};
}

bool SymbolLayer::prepare(QRhiResourceUpdateBatch* updates, const SymbolFrame& frame,
                          const Camera& camera, QSize pixelSize, double devicePixelRatio,
                          QString* error)
{
    Impl& d = *m_impl;
    if (d.rhi == nullptr) {
        return false;
    }
    if (!d.uploaded) {
        updates->uploadStaticBuffer(d.molds.get(), d.moldData.data());
        d.uploaded = true;
    }
    d.visible = frame.visible;
    if (frame.symbols != d.set || frame.version != d.version) {
        d.set = frame.symbols;
        d.version = frame.version;
        d.instances.reset();
        d.instanceCount = {};
        if (d.set != nullptr && !d.set->empty()) {
            // Positions from the middle of the symbols, for float precision.
            Eigen::AlignedBox3d box;
            for (const auto& s : d.set->supports) {
                box.extend(s.at);
            }
            for (const auto& f : d.set->forces) {
                box.extend(f.at);
            }
            for (const auto& m : d.set->moments) {
                box.extend(m.at);
            }
            for (const auto& l : d.set->distributed) {
                box.extend(l.from);
                box.extend(l.to);
            }
            for (const auto& a : d.set->axes) {
                box.extend(a.at);
            }
            d.origin =
                box.isEmpty() ? Eigen::Vector3d::Zero().eval() : Eigen::Vector3d(box.center());
            const detail::SymbolInstances all = detail::instancesOf(*d.set, d.origin);
            std::vector<SymbolInstance> data;
            for (std::size_t m = 0; m < kMoldCount; ++m) {
                d.instanceFirst.at(m) = static_cast<quint32>(data.size() * sizeof(SymbolInstance));
                d.instanceCount.at(m) = static_cast<int>(all.at(m).size());
                data.insert(data.end(), all.at(m).begin(), all.at(m).end());
            }
            if (!data.empty()) {
                const auto size = static_cast<quint32>(data.size() * sizeof(SymbolInstance));
                d.instances.reset(
                    d.rhi->newBuffer(QRhiBuffer::Static, QRhiBuffer::VertexBuffer, size));
                if (!d.instances->create()) {
                    *error =
                        QStringLiteral("could not create a symbol buffer of %1 bytes").arg(size);
                    return false;
                }
                updates->uploadStaticBuffer(d.instances.get(), data.data());
            }
        }
    }

    // The scale of a mold: its size in pixels times the length of a pixel
    // at its depth, 2 w / (P11 H), computed in the shader.
    const Mat4 view = camera.viewMatrixRelativeToTarget();
    const Mat4 projection = camera.projectionMatrix();
    const QMatrix4x4 viewProjection =
        d.rhi->clipSpaceCorrMatrix() * detail::toQt(detail::toOpenGlDepth(projection) * view);
    const Eigen::Vector3d offset = d.origin - camera.target();
    std::array<float, kUniformFloats> data{};
    detail::copyMatrix(viewProjection, data.data());
    std::size_t i = kMatrixFloats;
    data.at(i++) = static_cast<float>(offset.x());
    data.at(i++) = static_cast<float>(offset.y());
    data.at(i++) = static_cast<float>(offset.z());
    data.at(i++) = 0.0F;
    data.at(i++) = 2.0F / static_cast<float>(std::max(pixelSize.height(), 1));
    data.at(i++) = static_cast<float>(std::abs(projection(1, 1)));
    data.at(i++) = static_cast<float>(devicePixelRatio);
    data.at(i++) = 0.0F;
    updates->updateDynamicBuffer(d.uniforms.get(), 0, static_cast<quint32>(sizeof(data)),
                                 data.data());
    return true;
}

void SymbolLayer::draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const
{
    const Impl& d = *m_impl;
    if (d.rhi == nullptr || !d.visible || !d.instances) {
        return;
    }
    commandBuffer->setGraphicsPipeline(d.pipeline.get());
    commandBuffer->setViewport(viewport);
    commandBuffer->setShaderResources();
    // One draw per mold.
    for (std::size_t m = 0; m < kMoldCount; ++m) {
        if (d.instanceCount.at(m) == 0 || d.moldCount.at(m) == 0) {
            continue;
        }
        const std::array<QRhiCommandBuffer::VertexInput, 2> inputs = {{
            {d.molds.get(), d.moldFirst.at(m) * static_cast<quint32>(sizeof(MoldVertex))},
            {d.instances.get(), d.instanceFirst.at(m)},
        }};
        commandBuffer->setVertexInput(0, 2, inputs.data());
        commandBuffer->draw(d.moldCount.at(m), static_cast<quint32>(d.instanceCount.at(m)));
    }
}

std::array<int, detail::kMoldCount> SymbolLayer::instanceCounts() const
{
    return m_impl->instanceCount;
}

} // namespace fem::render
