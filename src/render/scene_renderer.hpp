#pragma once

#include <fem/render/camera.hpp>

#include <QSize>
#include <QString>

#include <memory>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiRenderTarget;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;

namespace fem::render {

struct FrameParams
{
    const Camera* camera = nullptr;
    QSize pixelSize;               // size of the render target, in physical pixels
    double devicePixelRatio = 1.0; // physical pixels per logical pixel
    bool showGrid = true;
    bool showAxes = true;
};

// Owns the GPU resources of the viewport and draws a frame into any render
// target. It knows nothing about widgets, so it also runs against an
// offscreen target with the Null backend (REQ-002-29).
class SceneRenderer
{
public:
    SceneRenderer();
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;
    SceneRenderer(SceneRenderer&&) = delete;
    SceneRenderer& operator=(SceneRenderer&&) = delete;

    // Creates pipelines and buffers for render targets compatible with
    // `renderPass` and `sampleCount`. Returns false, with errorString() set,
    // when a shader or a GPU resource cannot be created.
    [[nodiscard]] bool initialize(QRhi* rhi, QRhiRenderPassDescriptor* renderPass, int sampleCount);
    [[nodiscard]] bool isInitialized() const noexcept { return m_rhi != nullptr; }
    [[nodiscard]] const QString& errorString() const noexcept { return m_error; }

    // Records one frame: grid, then the axis indicator.
    void render(QRhiCommandBuffer* commandBuffer, QRhiRenderTarget* target,
                const FrameParams& params);

    void release();

private:
    [[nodiscard]] bool createGrid(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                                  QRhiResourceUpdateBatch* updates);
    [[nodiscard]] bool createGizmo(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                                   QRhiResourceUpdateBatch* updates);
    [[nodiscard]] bool createLabels(QRhiRenderPassDescriptor* renderPass, int sampleCount,
                                    QRhiResourceUpdateBatch* updates);
    [[nodiscard]] bool updateLabelAtlas(double devicePixelRatio, QRhiResourceUpdateBatch* updates);
    void updateGridUniforms(const FrameParams& params, QRhiResourceUpdateBatch* updates);
    void updateGizmoUniforms(const FrameParams& params, QRhiResourceUpdateBatch* updates);

    QRhi* m_rhi = nullptr;
    QString m_error;
    // Uploads of static data recorded by initialize(), submitted with the
    // first frame. Owned by the QRhi until then.
    QRhiResourceUpdateBatch* m_pendingUpdates = nullptr;

    std::unique_ptr<QRhiBuffer> m_gridVertices;
    std::unique_ptr<QRhiBuffer> m_gridUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_gridBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_gridPipeline;

    std::unique_ptr<QRhiBuffer> m_gizmoVertices;
    std::unique_ptr<QRhiBuffer> m_gizmoUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_gizmoBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_gizmoPipeline;
    int m_gizmoVertexCount = 0;

    std::unique_ptr<QRhiBuffer> m_labelVertices;
    std::unique_ptr<QRhiBuffer> m_labelUniforms;
    std::unique_ptr<QRhiTexture> m_labelAtlas;
    std::unique_ptr<QRhiSampler> m_labelSampler;
    std::unique_ptr<QRhiShaderResourceBindings> m_labelBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_labelPipeline;
    int m_labelVertexCount = 0;
    int m_labelAtlasCellSize = 0;
};

} // namespace fem::render
