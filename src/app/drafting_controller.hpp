#pragma once

#include <fem/geom/commands.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/geom/point_resolver.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/render/camera.hpp>
#include <fem/render/mesh_scene.hpp>

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QUndoStack;

namespace fem::render {
class Viewport;
} // namespace fem::render

namespace fem::app {

class CommandLineWidget;
class LineMeshJob;

// Ties the drafting pieces together (design 005 §5.7, §6): the command line,
// the viewport, the command session, the point resolver and the undo stack.
class DraftingController : public QObject
{
    Q_OBJECT

public:
    DraftingController(render::Viewport* viewport, CommandLineWidget* commandLine,
                       QObject* parent = nullptr);
    ~DraftingController() override;
    DraftingController(const DraftingController&) = delete;
    DraftingController& operator=(const DraftingController&) = delete;
    DraftingController(DraftingController&&) = delete;
    DraftingController& operator=(DraftingController&&) = delete;

    [[nodiscard]] QUndoStack* undoStack() const noexcept { return m_undo; }
    [[nodiscard]] const geom::DraftingSettings& settings() const noexcept { return m_settings; }
    [[nodiscard]] const geom::Drawing& drawing() const noexcept { return *m_drawing; }
    [[nodiscard]] bool isCommandActive() const noexcept { return m_session.isActive(); }
    // REQ-011-17: asked first on Esc; true when it took the key (the
    // cancel of a running analysis), and the drawing does not see it.
    void setEscapeHandler(std::function<bool()> handler) { m_escapeHandler = std::move(handler); }

public slots:
    // Menus, toolbar and shortcuts.
    void run(fem::geom::CommandName name);
    void enterText(const QString& text);
    void escape();
    // REQ-005-07: right click on the viewport.
    void cancelCommand();
    void deleteSelection();
    void undo();
    void redo();

    void setObjectSnap(bool on);
    void setSnapMode(fem::geom::SnapMode mode, bool on);
    void setGridSnap(bool on);
    void setOrtho(bool on);
    void setGridStep(double step);
    void setAperture(double pixels);
    // REQ-009-15: another mesh (an import) replaced the one of MESH.
    void meshReplaced();

signals:
    // REQ-005-24: the point a click would give, for the status bar.
    void cursorPointChanged(const QString& coordinates);
    void settingsChanged();
    // REQ-009-13: the MESH command put a mesh in the viewport.
    void meshGenerated(int nodes, int elements, int groups);
    // Feature 010 (design §5.8): the mesh of MESH, with its map to the
    // drawing and the revision of the drawing meshed.
    void drawingMeshed(std::shared_ptr<const fem::render::MeshScene> scene,
                       std::shared_ptr<const fem::mesher::LineMeshMap> map, std::uint64_t revision);

private:
    std::function<bool()> m_escapeHandler;
    void cursorMoved(render::Vec2 pixel);
    void pointRequested(render::Vec2 pixel);
    void drawingSelectionChanged();
    void startCommand(geom::CommandName name);
    // After every call into the session: messages, undo steps, prompt,
    // viewport mode and preview.
    void sync();
    void updatePreview();
    [[nodiscard]] QString promptText() const;
    [[nodiscard]] static QString messageText(const geom::Message& message);
    // Feature 009: the MESH command.
    void startMeshPrompt();
    void cancelMeshPrompt();
    void enterMeshSize(const QString& text);
    void warnIfMeshOutdated();

    QPointer<render::Viewport> m_viewport;
    QPointer<CommandLineWidget> m_commandLine;
    std::shared_ptr<geom::Drawing> m_drawing;
    geom::CommandSession m_session;
    geom::PointResolver m_resolver;
    geom::DraftingSettings m_settings;
    QUndoStack* m_undo = nullptr;
    std::optional<geom::ResolvedPoint> m_cursor;
    std::uint64_t m_shownRevision = 0;
    // Feature 009: the size prompt of MESH, its job, and the revision of the
    // drawing in the mesh shown (REQ-009-15).
    bool m_meshPrompt = false;
    LineMeshJob* m_meshJob = nullptr;
    std::optional<std::uint64_t> m_meshedRevision;
    bool m_outdatedWarned = false;
};

} // namespace fem::app
