#include "field_editor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

#include <array>
#include <memory>

namespace fem::app {

namespace {

struct FieldText
{
    Q_DECLARE_TR_FUNCTIONS(FieldEditor)
};

} // namespace

void addFieldEditor(QFormLayout* form, QWidget* body, const Field& field, const FieldValue& value,
                    const QString& unit, const CommitField& commit)
{
    // NOLINTBEGIN(cppcoreguidelines-owning-memory): the widgets are owned by
    // `body`.
    const auto withUnit = [&](QWidget* editor) -> QWidget* {
        if (unit.isEmpty()) {
            return editor;
        }
        auto* row = new QWidget(body);
        auto* box = new QHBoxLayout(row);
        box->setContentsMargins(0, 0, 0, 0);
        box->addWidget(editor, 1);
        box->addWidget(new QLabel(unit, row));
        return row;
    };
    switch (field.kind) {
    case FieldKind::Number: {
        auto* line = new QLineEdit(body);
        if (const auto* d = std::get_if<double>(&value)) {
            line->setText(formatNumber(*d));
        }
        if (field.optional) {
            line->setPlaceholderText(FieldText::tr("(none)"));
        }
        QObject::connect(line, &QLineEdit::editingFinished, body, [field, line, commit] {
            const auto parsed = parseNumber(line->text());
            if (!parsed || (!field.optional && std::holds_alternative<std::monostate>(*parsed))) {
                markInvalid(line, true);
                return;
            }
            commit(field, *parsed, line);
        });
        form->addRow(field.label, withUnit(line));
        break;
    }
    case FieldKind::Vector: {
        auto* row = new QWidget(body);
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
            QObject::connect(line, &QLineEdit::editingFinished, body, [field, lines, row, commit] {
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
                commit(field, FieldValue{typed}, row);
            });
        }
        form->addRow(field.label, withUnit(row));
        break;
    }
    case FieldKind::Flag: {
        auto* check = new QCheckBox(body);
        check->setChecked(std::get<bool>(value));
        QObject::connect(check, &QCheckBox::toggled, body,
                         [field, check, commit](bool on) { commit(field, FieldValue{on}, check); });
        form->addRow(field.label, check);
        break;
    }
    case FieldKind::Choice: {
        auto* combo = new QComboBox(body);
        combo->addItems(field.choices);
        combo->setCurrentIndex(std::get<int>(value));
        // activated: only the user's choices, not the setup above.
        QObject::connect(combo, &QComboBox::activated, body, [field, combo, commit](int index) {
            commit(field, FieldValue{index}, combo);
        });
        form->addRow(field.label, combo);
        break;
    }
    case FieldKind::Text: {
        auto* line = new QLineEdit(std::get<QString>(value), body);
        QObject::connect(line, &QLineEdit::editingFinished, body,
                         [field, line, commit] { commit(field, FieldValue{line->text()}, line); });
        form->addRow(field.label, line);
        break;
    }
    case FieldKind::Info: {
        const auto* shown = std::get_if<QString>(&value);
        if (shown != nullptr && !shown->isEmpty()) {
            auto* label = new QLabel(*shown, body);
            label->setWordWrap(true);
            form->addRow(field.label, label);
        }
        break;
    }
    }
    // NOLINTEND(cppcoreguidelines-owning-memory)
}

void markInvalid(QWidget* editor, bool invalid)
{
    editor->setStyleSheet(invalid ? QStringLiteral("border: 1px solid #d04040;") : QString());
    editor->setToolTip(invalid ? FieldText::tr("Invalid value: it was not applied.") : QString());
}

} // namespace fem::app
