#include "model_controller.hpp"

#include "section_geometry.hpp"

#include <fem/setup/editing.hpp>

#include <QUndoCommand>
#include <QUndoStack>

#include <utility>
#include <vector>

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
{
    m_symbolTimer.setSingleShot(true);
    m_symbolTimer.setInterval(0);
    connect(&m_symbolTimer, &QTimer::timeout, this, &ModelController::rebuildSymbols);
    // The drawing shares the undo stack: its edits move the symbols too.
    connect(m_undo, &QUndoStack::indexChanged, this, &ModelController::scheduleSymbols);
}

void ModelController::setSymbolOptions(SymbolOptions options)
{
    const bool sameModel =
        options.axes == m_symbolOptions.axes && options.axesOf == m_symbolOptions.axesOf;
    m_symbolOptions = std::move(options);
    // Feature 012 (NFR-012-02): only the loads or the reactions changed: no
    // rebuild of the model's symbols, unless one is due anyway.
    if (sameModel && m_modelSymbols && !m_symbolTimer.isActive()) {
        m_symbols = std::make_shared<const render::SymbolSet>(
            withResults(*m_modelSymbols, m_symbolOptions));
        emit symbolsChanged();
        return;
    }
    scheduleSymbols();
}

void ModelController::scheduleSymbols()
{
    // Many changes in one turn (an edit and its index) build them once.
    m_symbolTimer.start();
}

void ModelController::rebuildSymbols()
{
    m_symbolTimer.stop();
    SymbolOptions model = m_symbolOptions;
    model.loadsShown = true;
    model.reactionForces.clear();
    model.reactionMoments.clear();
    m_modelSymbols = std::make_shared<const render::SymbolSet>(
        buildSymbols(m_document, m_drawing, m_mesh, model));
    m_symbols =
        std::make_shared<const render::SymbolSet>(withResults(*m_modelSymbols, m_symbolOptions));
    emit symbolsChanged();
}

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

setup::Targets ModelController::targetsFrom(std::span<const geom::EntityId> entities,
                                            const render::SelectionItems& picked) const
{
    setup::Targets targets;
    if (!entities.empty()) {
        targets.entities.assign(entities.begin(), entities.end());
        return targets;
    }
    if (!m_mesh.mesh) {
        return targets;
    }
    const core::Mesh& mesh = *m_mesh.mesh;
    for (const int e : picked[render::SelectionKind::Element]) {
        if (e >= 0 && e < mesh.elementCount()) {
            targets.elements.push_back(mesh.elementId(e));
        }
    }
    for (const int n : picked[render::SelectionKind::Node]) {
        if (n >= 0 && n < mesh.nodeCount()) {
            targets.nodes.push_back(mesh.nodeId(n));
        }
    }
    targets.meshGeneration = m_mesh.generation;
    return targets;
}

std::vector<setup::GroupTarget> ModelController::meshGroups(int dimension) const
{
    std::vector<setup::GroupTarget> groups;
    if (m_mesh.mesh) {
        for (const core::Group& group : m_mesh.mesh->groups()) {
            if (group.dimension == dimension) {
                groups.push_back({.name = group.name, .dimension = group.dimension});
            }
        }
    }
    return groups;
}

std::optional<setup::ItemId> ModelController::add(setup::Item item, const QString& text)
{
    const setup::EditResult edit = setup::addItem(m_document, std::move(item));
    if (!edit || edit->added.empty()) {
        if (!edit) {
            emit refused(edit.error());
        }
        return std::nullopt;
    }
    const setup::ItemId id = setup::idOf(edit->added.front());
    push(*edit, text);
    return id;
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
    scheduleSymbols();
}

void ModelController::meshImported(const std::shared_ptr<const render::MeshScene>& scene)
{
    m_mesh.mesh = scene ? scene->sharedMesh() : nullptr;
    m_mesh.map = nullptr;
    ++m_mesh.generation;
    m_mesh.drawingRevision.reset();
    emit meshChanged();
    scheduleSymbols();
}

void ModelController::changed()
{
    // REQ-011-11: the results of the items that left the document go too.
    std::vector<setup::ItemId> gone;
    for (const setup::AnalysisResult& r : m_results.all()) {
        if (m_document.find(r.item) == nullptr) {
            gone.push_back(r.item);
        }
    }
    for (const setup::ItemId item : gone) {
        m_results.erase(item);
    }
    emit documentChanged();
    if (!gone.empty()) {
        emit resultsChanged();
    }
    scheduleSymbols();
}

void ModelController::setAnalysis(const setup::AnalysisSettings& settings)
{
    push(setup::changeAnalysis(m_document, settings), tr("analysis settings"));
}

setup::AnalysedState ModelController::state() const noexcept
{
    return {.documentRevision = m_document.revision(),
            .drawingRevision = m_drawing.revision(),
            .meshGeneration = m_mesh.generation};
}

void ModelController::caseFinished(const setup::CaseOutcome& outcome)
{
    if (outcome.result && m_document.find(outcome.result->item) != nullptr) {
        m_results.put(*outcome.result);
    }
    emit resultsChanged();
}

} // namespace fem::app
