#include "model_controller.hpp"

#include "section_geometry.hpp"

#include <fem/setup/editing.hpp>

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

std::optional<QString>
ModelController::setSolidFromPolylines(setup::ItemId id, std::span<const geom::EntityId> entities)
{
    const setup::Item* item = m_document.find(id);
    const auto* section = item != nullptr ? std::get_if<setup::SectionItem>(item) : nullptr;
    if (section == nullptr) {
        return tr("The item is not a section.");
    }
    auto made = solidFromPolylines(m_drawing, entities);
    if (auto* problem = std::get_if<std::string>(&made)) {
        return QString::fromStdString(*problem);
    }
    auto solid = std::get<core::SolidPolygon>(std::move(made));
    // The mesh size of a polygon edited before is kept.
    if (section->data.shape) {
        if (const auto* former = std::get_if<core::SolidPolygon>(&*section->data.shape)) {
            solid.meshSize = former->meshSize;
        }
    }
    // A shape and generic properties together are a problem of the model:
    // only the name stays.
    setup::SectionItem changed{.id = section->id, .data = {}};
    changed.data.name = section->data.name;
    changed.data.shape = std::move(solid);
    push(setup::changeItem(m_document, changed), tr("section from polylines"));
    return std::nullopt;
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
