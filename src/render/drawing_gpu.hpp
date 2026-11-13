#pragma once

#include <fem/geom/commands.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/render/camera.hpp>

#include <QSize>
#include <QString>

#include <cstdint>
#include <memory>
#include <vector>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiViewport;

namespace fem::render {

// What the renderer needs to know about the drawing for one frame.
struct DrawingFrame
{
    const geom::Drawing* drawing = nullptr;
    std::uint64_t selectionVersion = 0; // changes with the selection
    const std::vector<geom::EntityId>* selection = nullptr;
    const geom::Preview* preview = nullptr;
    std::uint64_t previewVersion = 0; // changes with the preview
};

// GPU buffers that draw the drawing entities, their highlight and the
// preview of the running command (REQ-005-18, 33; design 005 §5.5). Uses the
// line and point shaders of the mesh.
class DrawingGpu
{
public:
    DrawingGpu();
    ~DrawingGpu();
    DrawingGpu(const DrawingGpu&) = delete;
    DrawingGpu& operator=(const DrawingGpu&) = delete;
    DrawingGpu(DrawingGpu&&) = delete;
    DrawingGpu& operator=(DrawingGpu&&) = delete;

    [[nodiscard]] bool create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                              QString* error);
    void release();

    // Records the uploads needed for `frame` and the uniforms for `camera`.
    [[nodiscard]] bool prepare(QRhiResourceUpdateBatch* updates, const DrawingFrame& frame,
                               const Camera& camera, QSize pixelSize, double devicePixelRatio,
                               QString* error);
    // Entities depth tested; the preview on top.
    void draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::render
