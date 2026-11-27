#pragma once

// Feature 012 (design §5.5): the GPU pass of a result: the segments of the
// bars moved by scale x displacements and colored by a quantity, the
// original shape under them, and the selected nodes. Private to the render
// module.

#include <fem/render/camera.hpp>
#include <fem/render/result_display.hpp>
#include <fem/render/result_scene.hpp>
#include <fem/render/selection.hpp>

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

// What the renderer needs to know about the result for one frame.
struct ResultFrame
{
    const ResultScene* scene = nullptr;
    std::uint64_t sceneVersion = 0; // changes when the scene is replaced
    const std::vector<float>* values = nullptr;
    std::uint64_t valueVersion = 0; // changes with the values
    // The selection, highlighted when the result is on the mesh shown.
    const Selection* selection = nullptr;
    std::uint64_t selectionVersion = 0;
    bool highlight = false;
    ResultDisplay display;
    double scale = 1.0; // the factor of this frame (the animation applied)
};

class ResultLayer
{
public:
    ResultLayer();
    ~ResultLayer();
    ResultLayer(const ResultLayer&) = delete;
    ResultLayer& operator=(const ResultLayer&) = delete;
    ResultLayer(ResultLayer&&) = delete;
    ResultLayer& operator=(ResultLayer&&) = delete;

    [[nodiscard]] bool create(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount,
                              QString* error);
    void release();

    // Records the uploads that `frame` needs (only what changed) and the
    // uniforms.
    [[nodiscard]] bool prepare(QRhiResourceUpdateBatch* updates, const ResultFrame& frame,
                               const Camera& camera, QSize pixelSize, double devicePixelRatio,
                               QString* error);
    // The original shape, then the colored one, then the selected nodes,
    // depth tested.
    void draw(QRhiCommandBuffer* commandBuffer, const QRhiViewport& viewport) const;

    [[nodiscard]] bool hasScene() const noexcept;

    // REQ-012-08, 13 (for the tests): the buffers sent so far.
    struct Uploads
    {
        int geometry = 0;
        int values = 0;
        int flags = 0;
    };
    [[nodiscard]] Uploads uploads() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::render
