#pragma once

#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiShaderResourceBindings;
class QRhiViewport;

namespace fem::render {

// What the renderer needs to know about the mesh for one frame.
struct MeshFrame
{
    const MeshScene* scene = nullptr;
    std::uint64_t sceneVersion = 0; // changes when the scene is replaced
    std::uint64_t colorVersion = 0; // changes with the color mode or the selection
    std::uint64_t nodeVersion = 0;  // changes with showNodes or the visible dimensions
    DisplayOptions display;
    const Selection* selection = nullptr;
};

// GPU buffers and pipelines that draw a MeshScene: faces, lines, 0D points,
// shown nodes and the selected edges and nodes (design sections 5.2, 12.5).
class MeshGpu
{
public:
    MeshGpu();
    ~MeshGpu();
    MeshGpu(const MeshGpu&) = delete;
    MeshGpu& operator=(const MeshGpu&) = delete;
    MeshGpu(MeshGpu&&) = delete;
    MeshGpu& operator=(MeshGpu&&) = delete;

    [[nodiscard]] bool create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                              QString* error);
    void release();

    // Records the uploads needed for `frame` and the uniforms for `camera`.
    [[nodiscard]] bool prepare(QRhiResourceUpdateBatch* updates, const MeshFrame& frame,
                               const Camera& camera, QSize pixelSize, double devicePixelRatio,
                               QString* error);

    // Faces, lines, 0D points and shown nodes, depth tested.
    void draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport,
              const DisplayOptions& display) const;
    // Selected edges and nodes, on top of everything.
    void drawOverlay(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const;

    [[nodiscard]] bool hasScene() const noexcept { return m_hasScene; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_hasScene = false;
};

} // namespace fem::render
