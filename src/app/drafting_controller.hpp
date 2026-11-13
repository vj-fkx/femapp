#pragma once

#include <fem/geom/commands.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/geom/point_resolver.hpp>
#include <fem/render/camera.hpp>

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <optional>

class QUndoStack;

namespace fem::render {
class Viewport;
} // namespace fem::render

namespace fem::app {

class CommandLineWidget;

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

signals:
    // REQ-005-24: the point a click would give, for the status bar.
    void cursorPointChanged(const QString& coordinates);
    void settingsChanged();

private:
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

    QPointer<render::Viewport> m_viewport;
    QPointer<CommandLineWidget> m_commandLine;
    std::shared_ptr<geom::Drawing> m_drawing;
    geom::CommandSession m_session;
    geom::PointResolver m_resolver;
    geom::DraftingSettings m_settings;
    QUndoStack* m_undo = nullptr;
    std::optional<geom::ResolvedPoint> m_cursor;
    std::uint64_t m_shownRevision = 0;
};

} // namespace fem::app
