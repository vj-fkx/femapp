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
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
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

// REQ-010-16: the actions of the Add load menu, in the order of
// setup::Action.
enum class LoadKind : std::uint8_t {
    Nodal,
    Distributed,
    Concentrated,
    SelfWeight,
    Temperature,
    Gradient,
    InitialStrain,
    Settlement
};
inline constexpr std::array<LoadKind, 8> kLoadKinds{
    LoadKind::Nodal,       LoadKind::Distributed, LoadKind::Concentrated,  LoadKind::SelfWeight,
    LoadKind::Temperature, LoadKind::Gradient,    LoadKind::InitialStrain, LoadKind::Settlement};

// A new action of `kind`: zero, but the self-weight, which is the gravity
// along -Y (vertical in the plane of the drawing) in `units`, a distributed
// load over the whole bar and a settlement of V.
[[nodiscard]] setup::Action newAction(LoadKind kind, setup::UnitSystem units);
// Whether the action acts on nodes (its targets are nodes) or on bars.
[[nodiscard]] bool loadOnNodes(LoadKind kind);

// REQ-010-17: the terms of a combination from the factors typed per load
// case: an empty or zero factor leaves the case out; none when a factor is
// not a number.
[[nodiscard]] std::optional<std::vector<std::pair<setup::ItemId, double>>>
termsOf(std::span<const std::pair<setup::ItemId, QString>> typed);

} // namespace fem::app
