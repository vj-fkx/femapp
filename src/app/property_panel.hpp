#pragma once

// Feature 010 (design §5.5, REQ-010-19): the properties of the item
// selected in the model tree, as a form made from its fields.

#include "fields.hpp"

#include <fem/setup/document.hpp>

#include <QPointer>
#include <QWidget>

#include <optional>

class QFormLayout;

namespace fem::app {

class ModelController;

class PropertyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyPanel(ModelController* controller, QWidget* parent = nullptr);

    // The item to show; none for an empty panel.
    void showItem(std::optional<fem::setup::ItemId> id);

signals:
    // REQ-010-05: the outline of a solid polygon section from the closed
    // polylines selected in the drawing.
    void solidFromSelectionRequested(fem::setup::ItemId id);

private:
    void rebuild();
    void addField(QFormLayout* form, const Field& field, const setup::Item& item);
    // REQ-010-05, 06: what the core computes for a section, its drawing and
    // the editors of the thin-walled and polygon shapes.
    void addSection(QFormLayout* form, const setup::SectionItem& section);
    // A value typed or picked: written into a copy of the item and pushed,
    // or `editor` is marked invalid.
    void edit(const Field& field, const FieldValue& value, QWidget* editor);
    static void markInvalid(QWidget* editor, bool invalid);

    ModelController* m_controller = nullptr;
    std::optional<setup::ItemId> m_item;
    QPointer<QWidget> m_body;
};

} // namespace fem::app
