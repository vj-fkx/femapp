#include "model_controller.hpp"

#include <QUndoCommand>
#include <QUndoStack>

#include <utility>

namespace fem::app {

// REQ-010-23: one edit of the document in the undo stack shared with the
// drawing. Unlike a drafting command, the edit is not applied yet when it
// is pushed: the first redo applies it.
class DocumentUndoCommand : public QUndoCommand
{
public:
    DocumentUndoCommand(ModelController& controller, setup::DocumentEdit edit, const QString& text)
        : QUndoCommand(text)
        , m_controller(controller)
        , m_edit(std::move(edit))
    {}

    void undo() override
    {
        m_controller.m_document.revert(m_edit);
        m_controller.changed();
    }

    void redo() override
    {
        m_controller.m_document.apply(m_edit);
        m_controller.changed();
    }

private:
    ModelController& m_controller;
    setup::DocumentEdit m_edit;
};

ModelController::ModelController(QUndoStack* undo, const geom::Drawing& drawing, QObject* parent)
    : QObject(parent)
    , m_undo(undo)
    , m_drawing(drawing)
{}

bool ModelController::push(const setup::EditResult& edit, const QString& text)
{
    if (!edit) {
        emit refused(edit.error());
        return false;
    }
    push(*edit, text);
    return true;
}

void ModelController::push(setup::DocumentEdit edit, const QString& text)
{
    if (edit.empty()) {
        return;
    }
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): the stack takes it.
    m_undo->push(new DocumentUndoCommand(*this, std::move(edit), text));
}

void ModelController::meshGenerated(const std::shared_ptr<const render::MeshScene>& scene,
                                    std::shared_ptr<const mesher::LineMeshMap> map,
                                    std::uint64_t revision)
{
    // Design §5.8: every new mesh is a new generation; picked targets of the
    // former one become stale (REQ-010-13).
    m_mesh.mesh = scene ? scene->sharedMesh() : nullptr;
    m_mesh.map = std::move(map);
    ++m_mesh.generation;
    m_mesh.drawingRevision = revision;
    emit meshChanged();
}

void ModelController::meshImported(const std::shared_ptr<const render::MeshScene>& scene)
{
    m_mesh.mesh = scene ? scene->sharedMesh() : nullptr;
    m_mesh.map = nullptr;
    ++m_mesh.generation;
    m_mesh.drawingRevision.reset();
    emit meshChanged();
}

void ModelController::changed()
{
    emit documentChanged();
}

} // namespace fem::app
