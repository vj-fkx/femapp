#pragma once

// Feature 010 (design §5.5, §5.8): the model document of the window and the
// mesh it is built on. Every change goes through the undo stack of the
// drawing (REQ-010-23).

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/editing.hpp>

#include <QObject>
#include <QString>

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
    std::optional<QString> setSolidFromPolylines(setup::ItemId id,
                                                 std::span<const geom::EntityId> entities);

public slots:
    // REQ-010-13: a new mesh, from MESH (with its map and the revision of
    // the drawing meshed) or imported (without).
    void meshGenerated(const std::shared_ptr<const fem::render::MeshScene>& scene,
                       std::shared_ptr<const fem::mesher::LineMeshMap> map, std::uint64_t revision);
    void meshImported(const std::shared_ptr<const fem::render::MeshScene>& scene);

signals:
    // After every apply, undo and redo of a document edit.
    void documentChanged();
    void meshChanged();
    void refused(const fem::setup::EditRefusal& refusal);

private:
    friend class DocumentUndoCommand;
    void changed();

    QUndoStack* m_undo = nullptr;
    const geom::Drawing& m_drawing;
    setup::Document m_document;
    setup::MeshContext m_mesh;
};

} // namespace fem::app
