#pragma once

// Feature 010 (design §5.5): the editor of one field in a form, shared by
// the properties panel (each edit pushed) and the dialogs of the Model menu
// (the edits kept in a copy of the item until OK).

#include "fields.hpp"

#include <functional>

class QFormLayout;
class QWidget;

namespace fem::app {

// A value read from the editor `editor` of `field`; numbers are already
// parsed, an unreadable one is marked and not committed.
using CommitField =
    std::function<void(const Field& field, const FieldValue& value, QWidget* editor)>;

// A row of `form` with the editor of `field`, showing `value` with `unit`
// (empty for none); the widgets are children of `body`.
void addFieldEditor(QFormLayout* form, QWidget* body, const Field& field, const FieldValue& value,
                    const QString& unit, const CommitField& commit);

// A red border and a tip on an editor whose value was refused.
void markInvalid(QWidget* editor, bool invalid);

} // namespace fem::app
