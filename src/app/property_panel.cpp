#include "property_panel.hpp"

#include "model_controller.hpp"

#include <fem/setup/editing.hpp>
#include <fem/setup/units.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <array>
#include <memory>
#include <utility>

namespace fem::app {

PropertyPanel::PropertyPanel(ModelController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by this widget.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    // The document changed (an edit, an undo): show the item as it is now.
    connect(controller, &ModelController::documentChanged, this, &PropertyPanel::rebuild);
    rebuild();
}

void PropertyPanel::showItem(std::optional<setup::ItemId> id)
{
    m_item = id;
    rebuild();
}

void PropertyPanel::rebuild()
{
    // The editor that finished the edit may be the sender: delete later.
    if (m_body) {
        m_body->deleteLater();
    }
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): owned by their Qt parents.
    m_body = new QWidget(this);
    auto* form = new QFormLayout(m_body);
    // NOLINTEND(cppcoreguidelines-owning-memory)
    form->setContentsMargins(0, 0, 0, 0);
    layout()->addWidget(m_body);
    const setup::Item* item = m_item ? m_controller->document().find(*m_item) : nullptr;
    if (item == nullptr) {
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory): owned by the form.
        form->addRow(new QLabel(tr("Select an item of the model tree."), m_body));
        return;
    }
    for (const Field& field : fieldsOf(*item, m_controller->document())) {
        addField(form, field, *item);
    }
}

void PropertyPanel::addField(QFormLayout* form, const Field& field, const setup::Item& item)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): the widgets are owned by
    // the body of the panel.
    const FieldValue value = field.get(item);
    const QString unit =
        QString::fromStdString(setup::unitLabel(m_controller->document().units(), field.quantity));
    const auto withUnit = [&](QWidget* editor) -> QWidget* {
        if (unit.isEmpty()) {
            return editor;
        }
        auto* row = new QWidget(m_body);
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        box->addWidget(editor, 1);
        box->addWidget(new QLabel(unit, row));
        return row;
    };
    switch (field.kind) {
    case FieldKind::Number: {
        auto* line = new QLineEdit(m_body);
        if (const auto* d = std::get_if<double>(&value)) {
            line->setText(formatNumber(*d));
        }
        if (field.optional) {
            line->setPlaceholderText(tr("(none)"));
        }
        connect(line, &QLineEdit::editingFinished, this, [this, field, line] {
            const auto parsed = parseNumber(line->text());
            if (!parsed || (!field.optional && std::holds_alternative<std::monostate>(*parsed))) {
                markInvalid(line, true);
                return;
            }
            edit(field, *parsed, line);
        });
        form->addRow(field.label, withUnit(line));
        break;
    }
    case FieldKind::Vector: {
        auto* row = new QWidget(m_body);
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        const Eigen::Vector3d v = std::get<Eigen::Vector3d>(value);
        auto lines = std::make_shared<std::array<QLineEdit*, 3>>();
        for (int k = 0; k < 3; ++k) {
            auto* line = new QLineEdit(formatNumber(v(k)), row);
            lines->at(static_cast<std::size_t>(k)) = line;
            box->addWidget(line);
        }
        for (QLineEdit* line : *lines) {
            connect(line, &QLineEdit::editingFinished, this, [this, field, lines, row] {
                Eigen::Vector3d typed = Eigen::Vector3d::Zero();
                for (int k = 0; k < 3; ++k) {
                    const auto parsed = parseNumber(lines->at(static_cast<std::size_t>(k))->text());
                    const auto* d = parsed ? std::get_if<double>(&*parsed) : nullptr;
                    if (d == nullptr) {
                        markInvalid(row, true);
                        return;
                    }
                    typed(k) = *d;
                }
                edit(field, FieldValue{typed}, row);
            });
        }
        form->addRow(field.label, withUnit(row));
        break;
    }
    case FieldKind::Flag: {
        auto* check = new QCheckBox(m_body);
        check->setChecked(std::get<bool>(value));
        connect(check, &QCheckBox::toggled, this,
                [this, field, check](bool on) { edit(field, FieldValue{on}, check); });
        form->addRow(field.label, check);
        break;
    }
    case FieldKind::Choice: {
        auto* combo = new QComboBox(m_body);
        combo->addItems(field.choices);
        combo->setCurrentIndex(std::get<int>(value));
        // activated: only the user's choices, not the setup above.
        connect(combo, &QComboBox::activated, this,
                [this, field, combo](int index) { edit(field, FieldValue{index}, combo); });
        form->addRow(field.label, combo);
        break;
    }
    case FieldKind::Text: {
        auto* line = new QLineEdit(std::get<QString>(value), m_body);
        connect(line, &QLineEdit::editingFinished, this,
                [this, field, line] { edit(field, FieldValue{line->text()}, line); });
        form->addRow(field.label, line);
        break;
    }
    case FieldKind::Info: {
        const auto* shown = std::get_if<QString>(&value);
        if (shown != nullptr && !shown->isEmpty()) {
            form->addRow(field.label, new QLabel(*shown, m_body));
        }
        break;
    }
    }
    // NOLINTEND(cppcoreguidelines-owning-memory)
}

void PropertyPanel::edit(const Field& field, const FieldValue& value, QWidget* editor)
{
    if (!m_item || !field.set) {
        return;
    }
    const setup::Item* current = m_controller->document().find(*m_item);
    if (current == nullptr) {
        return;
    }
    // The same value again (the focus left a field): no edit to undo.
    if (field.get(*current) == value) {
        markInvalid(editor, false);
        return;
    }
    setup::Item changed = *current;
    // REQ-010-19: an invalid value stays in its field, marked, and nothing
    // goes to the document.
    if (!field.set(changed, value)) {
        markInvalid(editor, true);
        return;
    }
    m_controller->push(setup::changeItem(m_controller->document(), changed),
                       tr("edit %1").arg(field.label));
}

void PropertyPanel::markInvalid(QWidget* editor, bool invalid)
{
    editor->setStyleSheet(invalid ? QStringLiteral("border: 1px solid #d04040;") : QString());
    editor->setToolTip(invalid ? tr("Invalid value: it was not applied.") : QString());
}

} // namespace fem::app
