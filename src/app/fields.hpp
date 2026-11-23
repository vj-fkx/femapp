#pragma once

// Feature 010 (design §5.5, REQ-010-19): the fields of each kind of item,
// for the properties panel. A field reads and writes its item; the panel
// only lays the fields out, so the rules live here and are tested without
// widgets.

#include <fem/setup/document.hpp>
#include <fem/setup/units.hpp>

#include <QString>
#include <QStringList>

#include <Eigen/Core>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

namespace fem::app {

// The value of a field: empty (an optional value not given), a number, an
// integer index of a choice, a flag, a text or a vector.
using FieldValue = std::variant<std::monostate, double, int, bool, QString, Eigen::Vector3d>;

enum class FieldKind : std::uint8_t { Number, Choice, Flag, Text, Vector, Info };

// What a number may be.
enum class Range : std::uint8_t { Any, Positive, NonNegative, Poisson };

struct Field
{
    QString label;
    FieldKind kind = FieldKind::Number;
    setup::Quantity quantity; // Number and Vector
    bool optional = false;    // a Number that may be left empty
    Range range = Range::Any; // Number
    QStringList choices;      // Choice
    std::function<FieldValue(const setup::Item&)> get;
    // Writes a valid value; false (and the item untouched) for an invalid
    // one.
    std::function<bool(setup::Item&, const FieldValue&)> set;
};

// The fields of `item`; `document` gives the names to choose from (the
// materials and sections of an assignment).
[[nodiscard]] std::vector<Field> fieldsOf(const setup::Item& item, const setup::Document& document);

// A number typed in a field: the point as decimal separator, whatever the
// locale (as the command line of 005); empty text is std::monostate.
[[nodiscard]] std::optional<FieldValue> parseNumber(const QString& text);
// A number shown in a field, with up to 12 significant digits.
[[nodiscard]] QString formatNumber(double value);

// REQ-010-15: the shortcuts of the Supports menu. Fixed holds the
// translations and rotations, Pinned the translations, Roller the
// translation along the global Y (vertical in the plane of the drawing);
// Custom holds nothing, to be checked dof by dof.
enum class SupportPreset : std::uint8_t { Fixed, Pinned, Roller, Custom };

[[nodiscard]] setup::SupportItem supportOf(SupportPreset preset);

} // namespace fem::app
