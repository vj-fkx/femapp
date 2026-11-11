#pragma once

#include <fem/render/camera.hpp>

#include <QRhiWidget>
#include <QString>

#include <memory>

namespace fem::render {

// 3D viewport rendered on the GPU through Qt RHI.
//
// It picks the graphics backend (see planBackends()), turns mouse input into
// camera moves and draws the grid and the axis indicator. It only redraws
// when something changes.
//
// Mouse: middle button drag = pan, Shift + middle button drag = orbit,
// wheel = zoom around the cursor.
class Viewport : public QRhiWidget
{
    Q_OBJECT

public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;
    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;
    Viewport(Viewport&&) = delete;
    Viewport& operator=(Viewport&&) = delete;

    // False once every graphics backend has failed; see unavailableReason().
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] QString unavailableReason() const;

    // Name of the backend in use, e.g. "Direct3D 11"; empty until the first
    // frame is set up.
    [[nodiscard]] QString backendName() const;
    // Number of frames rendered so far.
    [[nodiscard]] quint64 frameCount() const;

    [[nodiscard]] bool isGridVisible() const;
    [[nodiscard]] bool isAxesVisible() const;
    [[nodiscard]] Projection projection() const;
    [[nodiscard]] const Camera& camera() const;

public slots:
    void setGridVisible(bool visible);
    void setAxesVisible(bool visible);
    void setStandardView(fem::render::StandardView view);
    void setProjection(fem::render::Projection projection);
    // Frames the whole scene; with nothing to show, the default grid region.
    void fitAll();

signals:
    void frameRendered();
    void backendChanged(const QString& name);
    // Emitted once, when no graphics backend could be initialized.
    void backendUnavailable(const QString& reason);

protected:
    void initialize(QRhiCommandBuffer* commandBuffer) override;
    void render(QRhiCommandBuffer* commandBuffer) override;
    void releaseResources() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void handleRenderFailed();
    void tryNextBackend(const QString& failure);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::render
