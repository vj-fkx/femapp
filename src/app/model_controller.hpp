#pragma once

// Feature 010 (design §5.5, §5.8): the model document of the window and the
// mesh it is built on. Every change goes through the undo stack of the
// drawing (REQ-010-23).

#include "symbol_builder.hpp"

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/render/selection.hpp>
#include <fem/render/symbols.hpp>
#include <fem/setup/analysis.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/editing.hpp>

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

class QUndoStack;

namespace fem::app {

class ModelController : public QObject
{
    Q_OBJECT

public:
    // `drawing` is the drawing of the window, alive as long as this object.
    ModelController(QUndoStack* undo, const geom::Drawing& drawing, QObject* parent = nullptr);

    [[nodiscard]] const setup::Document& document() const noexcept { return m_document; }
    // Only for building edits that need fresh ids (addItem, assign...); the
    // edit itself goes through push().
    [[nodiscard]] setup::Document& documentForIds() noexcept { return m_document; }
    [[nodiscard]] const setup::MeshContext& mesh() const noexcept { return m_mesh; }
    [[nodiscard]] const geom::Drawing& drawing() const noexcept { return m_drawing; }
    // The undo stack shared with the drawing: its changes may change the
    // state of targets (an entity erased).
    [[nodiscard]] QUndoStack* undoStack() const noexcept { return m_undo; }

    // An edit as one step of the undo stack, applied now. Refused edits are
    // reported by refused() and change nothing; returns whether it was
    // pushed.
    bool push(const setup::EditResult& edit, const QString& text);
    void push(setup::DocumentEdit edit, const QString& text);

    // REQ-010-05: section `id` becomes the solid polygon of the closed
    // polylines `entities` of the drawing (the largest the outline, the
    // others its holes), one step of undo. The problem as a text when it is
    // not a section or the entities are not closed polylines.
    // REQ-010-12: the targets of an assignment from the selection of the
    // viewport: the drawing entities, or else the elements and nodes picked
    // in the mesh (indices of the current mesh), by their external ids.
    [[nodiscard]] setup::Targets targetsFrom(std::span<const geom::EntityId> entities,
                                             const render::SelectionItems& picked) const;
    // The groups of dimension `dimension` of the current mesh.
    [[nodiscard]] std::vector<setup::GroupTarget> meshGroups(int dimension) const;
    // An item added as one step of undo: its id, or none when refused.
    std::optional<setup::ItemId> add(setup::Item item, const QString& text);

    // REQ-010-09, 22: the symbols of the model, rebuilt (once per turn of
    // the event loop) after every edit of the document or the drawing, a
    // new mesh or a new active case; symbolsChanged() then.
    [[nodiscard]] const std::shared_ptr<const render::SymbolSet>& symbols() const noexcept
    {
        return m_symbols;
    }
    void setSymbolOptions(SymbolOptions options);
    [[nodiscard]] const SymbolOptions& symbolOptions() const noexcept { return m_symbolOptions; }
    // Builds them now (the deferred rebuild does this).
    void rebuildSymbols();

    // REQ-011-01: the type and options of the analysis, as one step of undo
    // (nothing when they are the same).
    void setAnalysis(const setup::AnalysisSettings& settings);

    // REQ-011-11, 12: the results of the analyses, one per case or
    // combination, and the state now to tell whether each is current.
    [[nodiscard]] const setup::ResultSet& results() const noexcept { return m_results; }
    [[nodiscard]] setup::AnalysedState state() const noexcept;

    std::optional<QString> setSolidFromPolylines(setup::ItemId id,
                                                 std::span<const geom::EntityId> entities);

public slots:
    // REQ-010-13: a new mesh, from MESH (with its map and the revision of
    // the drawing meshed) or imported (without).
    void meshGenerated(const std::shared_ptr<const fem::render::MeshScene>& scene,
                       std::shared_ptr<const fem::mesher::LineMeshMap> map, std::uint64_t revision);
    void meshImported(const std::shared_ptr<const fem::render::MeshScene>& scene);
    // REQ-011-10, 11: an outcome of the analysis job. A result replaces the
    // item's, if the item is still in the document; a refusal or a failure
    // keeps the result before. resultsChanged() in every case.
    void caseFinished(const fem::setup::CaseOutcome& outcome);

signals:
    // After every apply, undo and redo of a document edit.
    void documentChanged();
    void meshChanged();
    void refused(const fem::setup::EditRefusal& refusal);
    void symbolsChanged();
    void resultsChanged();

private:
    friend class DocumentUndoCommand;
    void changed();

    QUndoStack* m_undo = nullptr;
    const geom::Drawing& m_drawing;
    setup::Document m_document;
    setup::MeshContext m_mesh;
    setup::ResultSet m_results;
    SymbolOptions m_symbolOptions;
    std::shared_ptr<const render::SymbolSet> m_symbols;
    // The symbols of the model with the loads and without reactions: the
    // display of a result changes only what it shows of them (012).
    std::shared_ptr<const render::SymbolSet> m_modelSymbols;
    QTimer m_symbolTimer;
    void scheduleSymbols();
};

} // namespace fem::app
