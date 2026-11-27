#pragma once

// Feature 010 (design §5.6): the GPU pass of the symbols. Each glyph is a
// mold of lines of unit size; each symbol an instance of it (position,
// basis, size in pixels, color). The vertex shader scales the mold by the
// clip w so that it keeps its size on the screen.

#include <fem/render/camera.hpp>
#include <fem/render/symbols.hpp>

#include <QSize>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiViewport;

namespace fem::render {

// What the renderer needs to know about the symbols for one frame.
struct SymbolFrame
{
    const SymbolSet* symbols = nullptr;
    std::uint64_t version = 0; // changes with the set
    bool visible = true;
};

namespace detail {

using Rgba = std::uint32_t; // 0xAABBGGRR

// A vertex of a mold: (x, y, z) in units of the symbol size, w = 1 when y
// is a length of the world instead (the link between distributed arrows);
// a color with alpha 0 takes the color of the instance.
struct MoldVertex
{
    std::array<float, 4> position;
    Rgba color;
};

enum class Mold : std::uint8_t { Fixed, Pinned, Roller, Generic, Arrow, Arc, Triad, Link };
inline constexpr std::size_t kMoldCount = 8;

// REQ-012-16: the color of the reactions (lime, apart from the green of the
// supports and the red of the forces).
inline constexpr Rgba kReactionColor = 120U | (230U << 8U) | (60U << 16U) | (0xFFU << 24U);

// Pairs of vertices (lines).
[[nodiscard]] const std::vector<MoldVertex>& moldVertices(Mold mold);

// One symbol: the mold at `at` (from the origin of the set), its x, y and
// z scaled by `size` pixels.
struct SymbolInstance
{
    std::array<float, 3> at;
    std::array<float, 3> x;
    std::array<float, 3> y;
    std::array<float, 3> z;
    float size;
    Rgba color;
};

using SymbolInstances = std::array<std::vector<SymbolInstance>, kMoldCount>;

// The instances of every mold for `set`, relative to `origin`. Distributed
// loads: 8 arrows along the bar, as long as their value over the largest
// one, and the links between their tails.
[[nodiscard]] SymbolInstances instancesOf(const SymbolSet& set, const Eigen::Vector3d& origin);

} // namespace detail

class SymbolLayer
{
public:
    SymbolLayer();
    ~SymbolLayer();
    SymbolLayer(const SymbolLayer&) = delete;
    SymbolLayer& operator=(const SymbolLayer&) = delete;
    SymbolLayer(SymbolLayer&&) = delete;
    SymbolLayer& operator=(SymbolLayer&&) = delete;

    [[nodiscard]] bool create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                              QString* error);
    void release();

    [[nodiscard]] bool prepare(QRhiResourceUpdateBatch* updates, const SymbolFrame& frame,
                               const Camera& camera, QSize pixelSize, double devicePixelRatio,
                               QString* error);
    // On top of the scene: no depth test.
    void draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const;

    // The instances drawn by the last prepare(), per mold.
    [[nodiscard]] std::array<int, detail::kMoldCount> instanceCounts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::render
