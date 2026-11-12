#pragma once

#include <fem/render/camera.hpp>
#include <fem/render/display_options.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>

#include <QRhiWidget>
#include <QString>

#include <memory>

namespace fem::render {

// 3D viewport rendered on the GPU through Qt RHI.
//
// It picks the graphics backend (see planBackends()), turns mouse input into
// camera moves and selections, and draws the mesh, the grid and the axis
// indicator. It only redraws when something changes.
//
// Mouse: middle button drag = pan, Shift + middle button drag = orbit,
// wheel = zoom around the cursor; left click = pick, left drag = window
// (left to right) or crossing (right to left), with Shift to remove.
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

    // REQ-004-05: the mesh shown; nullptr for none. Clears the selection.
    void setScene(std::shared_ptr<const MeshScene> scene);
    [[nodiscard]] const std::shared_ptr<const MeshScene>& scene() const;
    [[nodiscard]] const DisplayOptions& displayOptions() const;
    [[nodiscard]] const Selection& selection() const;
    [[nodiscard]] const SelectionFilter& selectionFilter() const;

    // REQ-004-19, 33: turns a selection kind on or off; turning one off takes
    // its items out of the selection. Returns false (and changes nothing) when
    // asked to turn off the last kind that is on.
    bool setSelectionKindEnabled(fem::render::SelectionKind kind, bool enabled);

    // REQ-004-37: only the selection is shown.
    [[nodiscard]] bool isIsolated() const;

public slots:
    void setGridVisible(bool visible);
    void setAxesVisible(bool visible);
    void setStandardView(fem::render::StandardView view);
    void setProjection(fem::render::Projection projection);
    // Frames the whole scene; with nothing to show, the default grid region.
    void fitAll();

    void setDisplayMode(fem::render::DisplayMode mode);
    void setColorMode(fem::render::ColorMode mode);
    void setDimensionVisible(int dimension, bool visible);
    // REQ-004-34
    void setNodesShown(bool shown);
    // REQ-004-37, 39: shows only the selection; refused (and reported by
    // isolationChanged) while the selection is empty.
    void setIsolated(bool isolated);
    void clearSelection();

signals:
    void frameRendered();
    void backendChanged(const QString& name);
    // Emitted once, when no graphics backend could be initialized.
    void backendUnavailable(const QString& reason);
    // REQ-004-27: the counts are read from selection().
    void selectionChanged();
    // First frame rendered with the current scene.
    void sceneShown();
    // REQ-004-39: the selection-only display was turned on or off.
    void isolationChanged(bool isolated);

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
    void finishPick(const QPointF& releasePosition, bool remove);
    void selectionEdited();
    void rebuildIsolation();
    void finishIsolatedPick(const QPointF& releasePosition, bool remove);

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fem::render
