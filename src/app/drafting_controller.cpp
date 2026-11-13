#include "drafting_controller.hpp"

#include "command_line_widget.hpp"

#include <fem/geom/command_input.hpp>
#include <fem/render/viewport.hpp>

#include <QStringList>
#include <QUndoCommand>
#include <QUndoStack>

#include <utility>
#include <vector>

namespace fem::app {

namespace {

// One finished drafting command in the undo history (REQ-005-31). The
// command already changed the drawing, so the first redo does nothing.
class DrawingUndoCommand : public QUndoCommand
{
public:
    DrawingUndoCommand(geom::Drawing& drawing, geom::DrawingEdit edit, const QString& text)
        : QUndoCommand(text)
        , m_drawing(drawing)
        , m_edit(std::move(edit))
    {}

    void undo() override { m_drawing.revert(m_edit); }

    void redo() override
    {
        if (m_first) {
            m_first = false;
            return;
        }
        m_drawing.apply(m_edit);
    }

private:
    geom::Drawing& m_drawing;
    geom::DrawingEdit m_edit;
    bool m_first = true;
};

QString commandName(geom::CommandName name)
{
    const std::string_view text = geom::commandText(name);
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString coordinate(double value)
{
    return QString::number(value, 'f', 4);
}

} // namespace

DraftingController::DraftingController(render::Viewport* viewport, CommandLineWidget* commandLine,
                                       QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
    , m_commandLine(commandLine)
    , m_drawing(std::make_shared<geom::Drawing>())
    , m_session(*m_drawing)
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by its Qt parent.
    , m_undo(new QUndoStack(this))
{
    m_viewport->setDrawing(m_drawing);
    connect(m_viewport, &render::Viewport::cursorMoved, this, &DraftingController::cursorMoved);
    connect(m_viewport, &render::Viewport::pointRequested, this,
            &DraftingController::pointRequested);
    connect(m_viewport, &render::Viewport::drawingSelectionChanged, this,
            &DraftingController::drawingSelectionChanged);
    connect(m_commandLine, &CommandLineWidget::textEntered, this, &DraftingController::enterText);
    connect(m_commandLine, &CommandLineWidget::escapePressed, this, &DraftingController::escape);
    connect(m_commandLine, &CommandLineWidget::deletePressed, this,
            &DraftingController::deleteSelection);
    connect(m_viewport, &render::Viewport::rightClicked, this, &DraftingController::cancelCommand);
    // Undo and redo change the drawing behind the session's back.
    connect(m_undo, &QUndoStack::indexChanged, this, [this] {
        if (m_viewport) {
            m_shownRevision = m_drawing->revision();
            m_viewport->drawingChanged();
        }
    });
    sync();
}

DraftingController::~DraftingController() = default;

void DraftingController::run(geom::CommandName name)
{
    m_commandLine->appendHistory(commandName(name));
    startCommand(name);
}

void DraftingController::startCommand(geom::CommandName name)
{
    // REQ-005-27: objects selected before the command are used by it.
    const std::vector<geom::EntityId> selection =
        m_viewport ? m_viewport->drawingSelection() : std::vector<geom::EntityId>{};
    m_session.start(name, selection);
    sync();
}

void DraftingController::enterText(const QString& text)
{
    m_commandLine->appendHistory(text.isEmpty() ? promptText()
                                                : promptText() + QLatin1Char(' ') + text);
    const std::string input = text.toStdString();
    // A command name with nothing running starts it with the current
    // selection; the rest goes to the session.
    if (!m_session.isActive()) {
        if (const auto name = geom::findCommand(input)) {
            startCommand(*name);
            return;
        }
    }
    geom::Vec3 direction = geom::Vec3::UnitX();
    if (m_cursor && m_session.referencePoint()) {
        direction = m_cursor->point - *m_session.referencePoint();
    }
    m_session.feedText(input, direction);
    sync();
}

void DraftingController::escape()
{
    if (m_session.isActive()) {
        m_session.cancel();
        m_commandLine->appendHistory(tr("*Cancel*"));
        sync();
        return;
    }
    // REQ-004-25, REQ-005-25: Esc clears the selections.
    if (m_viewport) {
        m_viewport->clearDrawingSelection();
        m_viewport->clearSelection();
    }
}

void DraftingController::cancelCommand()
{
    if (m_session.isActive()) {
        m_session.cancel();
        m_commandLine->appendHistory(tr("*Cancel*"));
        sync();
    }
}

void DraftingController::deleteSelection()
{
    // REQ-005-30
    if (!m_session.isActive() && m_viewport && !m_viewport->drawingSelection().empty()) {
        run(geom::CommandName::Erase);
    }
}

void DraftingController::undo()
{
    if (m_session.isActive()) {
        m_session.cancel();
        sync();
    }
    if (!m_undo->canUndo()) {
        m_commandLine->appendHistory(tr("Nothing to undo."));
        return;
    }
    m_undo->undo();
}

void DraftingController::redo()
{
    if (m_session.isActive()) {
        m_session.cancel();
        sync();
    }
    if (!m_undo->canRedo()) {
        m_commandLine->appendHistory(tr("Nothing to redo."));
        return;
    }
    m_undo->redo();
}

void DraftingController::setObjectSnap(bool on)
{
    m_settings.objectSnap = on;
    emit settingsChanged();
}

void DraftingController::setSnapMode(geom::SnapMode mode, bool on)
{
    m_settings.modes.at(static_cast<std::size_t>(mode)) = on;
    emit settingsChanged();
}

void DraftingController::setGridSnap(bool on)
{
    m_settings.gridSnap = on;
    emit settingsChanged();
}

void DraftingController::setOrtho(bool on)
{
    m_settings.ortho = on;
    emit settingsChanged();
}

void DraftingController::setGridStep(double step)
{
    if (step > 0.0) {
        m_settings.gridStep = step;
        emit settingsChanged();
    }
}

void DraftingController::setAperture(double pixels)
{
    if (pixels > 0.0) {
        m_settings.aperturePx = pixels;
        emit settingsChanged();
    }
}

void DraftingController::cursorMoved(render::Vec2 pixel)
{
    if (!m_viewport) {
        return;
    }
    // REQ-005-14, 19, 22, 23: the point a click would give.
    const render::Camera& camera = m_viewport->camera();
    const render::Ray ray = camera.rayThrough(pixel);
    geom::ScreenView view;
    view.viewProjection = camera.projectionMatrix() * camera.viewMatrix();
    view.width = camera.viewportWidth();
    view.height = camera.viewportHeight();
    view.orthographic = camera.projection() == render::Projection::Orthographic;
    view.rayOrigin = ray.origin;
    view.rayDirection = ray.direction;
    view.cursor = pixel;
    // Object snaps only while a point is asked for (REQ-005-19).
    geom::DraftingSettings settings = m_settings;
    settings.objectSnap = m_settings.objectSnap && m_session.wantsPoint();
    m_cursor = m_resolver.resolve(*m_drawing, view, settings, m_session.referencePoint());

    if (m_cursor) {
        emit cursorPointChanged(QStringLiteral("%1, %2, %3")
                                    .arg(coordinate(m_cursor->point.x()),
                                         coordinate(m_cursor->point.y()),
                                         coordinate(m_cursor->point.z())));
    } else {
        emit cursorPointChanged(QString());
    }
    if (m_cursor && m_cursor->snap) {
        m_viewport->setSnapMarker(
            render::SnapMarker{.point = m_cursor->point, .mode = *m_cursor->snap});
    } else {
        m_viewport->setSnapMarker(std::nullopt);
    }
    if (m_session.isActive()) {
        updatePreview();
    }
}

void DraftingController::pointRequested(render::Vec2 pixel)
{
    cursorMoved(pixel);
    if (m_cursor && m_session.wantsPoint()) {
        m_session.feedPoint(m_cursor->point);
        sync();
    }
}

void DraftingController::drawingSelectionChanged()
{
    if (m_session.wantsSelection() && m_viewport) {
        m_session.feedSelection(m_viewport->drawingSelection());
    }
}

void DraftingController::sync()
{
    for (const geom::Message& message : m_session.takeMessages()) {
        m_commandLine->appendHistory(messageText(message));
    }
    bool finished = false;
    for (geom::DrawingEdit& edit : m_session.takeFinishedEdits()) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the stack takes it.
        m_undo->push(new DrawingUndoCommand(*m_drawing, std::move(edit), tr("drafting")));
        finished = true;
    }
    if (const auto request = m_session.takeUndoRequest()) {
        if (*request == geom::CommandName::Undo) {
            undo();
        } else {
            redo();
        }
    }
    if (m_viewport) {
        if (m_drawing->revision() != m_shownRevision) {
            m_shownRevision = m_drawing->revision();
            m_viewport->drawingChanged();
        }
        // After a command the selection is dropped, as in AutoCAD.
        if (finished && !m_session.isActive()) {
            m_viewport->clearDrawingSelection();
        }
        m_viewport->setMouseMode(m_session.wantsPoint() ? render::MouseMode::PickPoint
                                                        : render::MouseMode::Select);
        m_viewport->setCommandSelection(m_session.wantsSelection());
        if (!m_session.wantsPoint()) {
            m_viewport->setSnapMarker(std::nullopt);
        }
        updatePreview();
    }
    m_commandLine->setPrompt(promptText());
}

void DraftingController::updatePreview()
{
    if (!m_viewport) {
        return;
    }
    const std::optional<geom::Vec3> cursor = m_cursor && m_session.wantsPoint()
                                                 ? std::optional<geom::Vec3>(m_cursor->point)
                                                 : std::nullopt;
    m_viewport->setDrawingPreview(m_session.preview(cursor));
}

QString DraftingController::promptText() const
{
    const geom::Prompt prompt = m_session.prompt();
    QString text;
    switch (prompt.id) {
    case geom::PromptId::Command:
        text = tr("Command:");
        break;
    case geom::PromptId::Point:
        text = tr("Specify a point");
        break;
    case geom::PromptId::FirstPoint:
        text = tr("Specify the first point");
        break;
    case geom::PromptId::NextPoint:
        text = tr("Specify the next point");
        break;
    case geom::PromptId::SelectObjects:
        text = tr("Select objects (Enter to finish)");
        break;
    case geom::PromptId::BasePoint:
        text = tr("Specify the base point");
        break;
    case geom::PromptId::SecondPoint:
        text = tr("Specify the second point");
        break;
    case geom::PromptId::SecondPointOrEnter:
        text = tr("Specify the second point or Enter to finish");
        break;
    }
    if (!prompt.options.empty()) {
        QStringList options;
        for (const geom::OptionId option : prompt.options) {
            options.append(option == geom::OptionId::Undo ? tr("Undo (U)") : tr("Close (C)"));
        }
        text += QLatin1Char(' ') + tr("or [%1]").arg(options.join(QLatin1Char('/')));
    }
    if (prompt.id != geom::PromptId::Command) {
        text += QLatin1Char(':');
    }
    return text;
}

QString DraftingController::messageText(const geom::Message& message)
{
    switch (message.id) {
    case geom::MessageId::InvalidInput:
        return tr("Invalid input.");
    case geom::MessageId::UnknownCommand:
        return tr("Unknown command.");
    case geom::MessageId::NoPreviousPoint:
        return tr("There is no previous point.");
    case geom::MessageId::NeedTwoSegments:
        return tr("At least two segments are needed.");
    case geom::MessageId::NothingSelected:
        return tr("No object selected.");
    case geom::MessageId::PointsCreated:
        return tr("%n point(s) created.", nullptr, message.count);
    case geom::MessageId::LinesCreated:
        return tr("%n line(s) created.", nullptr, message.count);
    case geom::MessageId::PolylineCreated:
        return tr("Polyline created.");
    case geom::MessageId::EntitiesMoved:
        return tr("%n object(s) moved.", nullptr, message.count);
    case geom::MessageId::EntitiesCopied:
        return tr("%n copy(ies) created.", nullptr, message.count);
    case geom::MessageId::EntitiesErased:
        return tr("%n object(s) erased.", nullptr, message.count);
    }
    return {};
}

} // namespace fem::app
