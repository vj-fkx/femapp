#include "fields.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Feature 010, T15 (design §5.5): REQ-010-19. The fields of the items read
// and write them, and refuse invalid values, without widgets.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::app::Field;
using fem::app::FieldKind;
using fem::app::FieldValue;

const Field& field(const std::vector<Field>& fields, const std::string& label)
{
    const auto it = std::ranges::find_if(
        fields, [&](const Field& f) { return f.label.toStdString() == label; });
    REQUIRE(it != fields.end());
    return *it;
}

} // namespace

TEST_CASE("the fields of a material", "[app][fields]")
{
    const s::Document document;
    s::Item item = s::MaterialItem{
        .id = s::ItemId{1},
        .data = {.name = "steel", .youngsModulus = 2.1e8},
        .standard = "EN 1993-1-1",
        .grade = "S355",
        .properties = {{.key = "fy", .quantity = s::quantity::kStress, .value = 3.55e5}}};
    const auto fields = fem::app::fieldsOf(item, document);
    const Field& e = field(fields, "E");
    CHECK(e.kind == FieldKind::Number);
    CHECK(e.quantity == s::quantity::kStress);
    CHECK(std::get<double>(e.get(item)) == 2.1e8);
    CHECK(e.set(item, FieldValue{2.0e8}));
    CHECK(std::get<s::MaterialItem>(item).data.youngsModulus == 2.0e8);
    // A negative modulus is refused and leaves the item alone.
    CHECK_FALSE(e.set(item, FieldValue{-1.0}));
    CHECK(std::get<s::MaterialItem>(item).data.youngsModulus == 2.0e8);
    // Optional: empty clears it.
    CHECK(e.set(item, FieldValue{}));
    CHECK_FALSE(std::get<s::MaterialItem>(item).data.youngsModulus.has_value());
    CHECK(std::holds_alternative<std::monostate>(e.get(item)));
    // Poisson's ratio below 0.5.
    const Field& nu = field(fields, "ν");
    CHECK_FALSE(nu.set(item, FieldValue{0.6}));
    CHECK(nu.set(item, FieldValue{0.3}));
    // The extra property, with its quantity.
    const Field& fy = field(fields, "fy");
    CHECK(fy.quantity == s::quantity::kStress);
    CHECK(fy.set(item, FieldValue{3.0e5}));
    CHECK(std::get<s::MaterialItem>(item).properties.front().value == 3.0e5);
    // The name may not be empty; the standard is shown only.
    const Field& name = field(fields, "Name");
    CHECK_FALSE(name.set(item, FieldValue{QStringLiteral("  ")}));
    CHECK(name.set(item, FieldValue{QStringLiteral("S355")}));
    CHECK(field(fields, "Standard").kind == FieldKind::Info);
    CHECK(field(fields, "Standard").set == nullptr);
}

TEST_CASE("the fields of an assignment choose among the names", "[app][fields]")
{
    s::Document document;
    s::DocumentEdit edit;
    edit.added = {s::MaterialItem{.id = s::ItemId{1}, .data = {.name = "steel"}},
                  s::MaterialItem{.id = s::ItemId{2}, .data = {.name = "concrete"}},
                  s::SectionItem{.id = s::ItemId{3}, .data = {.name = "IPE"}}};
    document.apply(edit);
    s::Item item = s::BarAssignment{
        .id = s::ItemId{4}, .targets = {}, .data = {.material = "concrete", .section = "gone"}};
    const auto fields = fem::app::fieldsOf(item, document);
    const Field& material = field(fields, "Material");
    REQUIRE(material.kind == FieldKind::Choice);
    CHECK(material.choices == QStringList{QStringLiteral("steel"), QStringLiteral("concrete")});
    CHECK(std::get<int>(material.get(item)) == 1);
    CHECK(material.set(item, FieldValue{0}));
    CHECK(std::get<s::BarAssignment>(item).data.material == "steel");
    CHECK_FALSE(material.set(item, FieldValue{5}));
    // A section that is not in the document is shown first.
    const Field& section = field(fields, "Section");
    CHECK(section.choices.front() == QStringLiteral("gone"));
    CHECK(std::get<int>(section.get(item)) == 0);
    // The kind, the warping flag.
    CHECK(field(fields, "Kind").set(item, FieldValue{3}));
    CHECK(std::get<s::BarAssignment>(item).data.kind == c::ElementKind::Beam3D);
    CHECK(field(fields, "Warping").set(item, FieldValue{true}));
    CHECK(std::get<s::BarAssignment>(item).data.warping);
}

TEST_CASE("the fields of loads and supports", "[app][fields]")
{
    const s::Document document;
    s::Item nodal = s::LoadItem{.id = s::ItemId{1},
                                .loadCase = s::ItemId{2},
                                .targets = {},
                                .action = c::NodalForce{.force = Eigen::Vector3d(1, 2, 3)}};
    const auto nodalFields = fem::app::fieldsOf(nodal, document);
    const Field& force = field(nodalFields, "Force");
    CHECK(force.kind == FieldKind::Vector);
    CHECK(force.quantity == s::quantity::kForce);
    CHECK(std::get<Eigen::Vector3d>(force.get(nodal)) == Eigen::Vector3d(1, 2, 3));
    CHECK(force.set(nodal, FieldValue{Eigen::Vector3d(0, 0, -5)}));
    CHECK(std::get<c::NodalForce>(std::get<s::LoadItem>(nodal).action).force.z() == -5.0);
    CHECK(field(nodalFields, "Moment").quantity == s::quantity::kMoment);

    // A settlement: the quantity of its value follows the dof.
    const s::Item down = s::LoadItem{.id = s::ItemId{3},
                                     .loadCase = s::ItemId{2},
                                     .targets = {},
                                     .action = s::Settlement{.dof = c::DofType::W, .value = -0.01}};
    CHECK(field(fem::app::fieldsOf(down, document), "Value").quantity == s::quantity::kLength);
    const s::Item turn =
        s::LoadItem{.id = s::ItemId{4},
                    .loadCase = s::ItemId{2},
                    .targets = {},
                    .action = s::Settlement{.dof = c::DofType::RX, .value = 0.001}};
    CHECK(field(fem::app::fieldsOf(turn, document), "Value").quantity == s::quantity::kNone);

    // A support: one flag per dof.
    s::Item support = s::SupportItem{.id = s::ItemId{5}, .targets = {}, .held = {}};
    const auto supportFields = fem::app::fieldsOf(support, document);
    CHECK(supportFields.size() == 7);
    CHECK(field(supportFields, "RZ").set(support, FieldValue{true}));
    CHECK(std::get<s::SupportItem>(support).held[static_cast<std::size_t>(c::DofType::RZ)]);

    // A load case: choosing another standard clears the category.
    s::Item office =
        s::LoadCase{.id = s::ItemId{6}, .name = "Office", .standard = "EN 1990", .category = "Q-B"};
    const auto caseFields = fem::app::fieldsOf(office, document);
    const Field& standard = field(caseFields, "Standard");
    const auto nbr = static_cast<int>(standard.choices.indexOf(QStringLiteral("ABNT NBR 8681")));
    REQUIRE(nbr > 0);
    const FieldValue psi = field(caseFields, "ψ0, ψ1, ψ2").get(office);
    REQUIRE(std::holds_alternative<QString>(psi));
    CHECK(std::get<QString>(psi) == QStringLiteral("0.7, 0.5, 0.3"));
    CHECK(standard.set(office, FieldValue{nbr}));
    CHECK(std::get<s::LoadCase>(office).standard == "ABNT NBR 8681");
    CHECK(std::get<s::LoadCase>(office).category.empty());
}

TEST_CASE("numbers typed in a field", "[app][fields]")
{
    CHECK(std::get<double>(*fem::app::parseNumber(QStringLiteral(" 2.5e3 "))) == 2500.0);
    CHECK(std::holds_alternative<std::monostate>(*fem::app::parseNumber(QString())));
    // The point is the decimal separator, whatever the locale.
    CHECK_FALSE(fem::app::parseNumber(QStringLiteral("2,5")).has_value());
    CHECK_FALSE(fem::app::parseNumber(QStringLiteral("abc")).has_value());
    CHECK(fem::app::formatNumber(0.1 + 0.2) == QStringLiteral("0.3"));
    CHECK(fem::app::formatNumber(210000.0) == QStringLiteral("210000"));
}

TEST_CASE("a truss assignment warns of buckling between its nodes", "[app][fields]")
{
    // Feature 010, T17: REQ-010-10.
    const s::Document document;
    const auto warning = [&](c::ElementKind kind) {
        const s::Item item =
            s::BarAssignment{.id = s::ItemId{1}, .targets = {}, .data = {.kind = kind}};
        for (const Field& f : fem::app::fieldsOf(item, document)) {
            if (f.label.toStdString() == "Warning") {
                return std::get<QString>(f.get(item));
            }
        }
        return QString();
    };
    CHECK_FALSE(warning(c::ElementKind::Truss2D).isEmpty());
    CHECK_FALSE(warning(c::ElementKind::Truss3D).isEmpty());
    CHECK(warning(c::ElementKind::Beam3D).isEmpty());
}

TEST_CASE("the shortcuts of the supports", "[app][fields]")
{
    // Feature 010, T18: REQ-010-15.
    using fem::app::SupportPreset;
    const auto held = [](SupportPreset preset) {
        std::string dofs;
        const auto support = fem::app::supportOf(preset);
        for (const bool h : support.held) {
            dofs += h ? '1' : '0';
        }
        return dofs; // U V W RX RY RZ T Warp
    };
    CHECK(held(SupportPreset::Fixed) == "11111100");
    CHECK(held(SupportPreset::Pinned) == "11100000");
    CHECK(held(SupportPreset::Roller) == "01000000");
    CHECK(held(SupportPreset::Custom) == "00000000");
    // The support fields show what the shortcut holds.
    const s::Document document;
    const s::Item item = fem::app::supportOf(SupportPreset::Pinned);
    const auto fields = fem::app::fieldsOf(item, document);
    CHECK(std::get<bool>(field(fields, "W").get(item)));
    CHECK_FALSE(std::get<bool>(field(fields, "RZ").get(item)));
}

TEST_CASE("a new load of each action", "[app][fields]")
{
    // Feature 010, T19: REQ-010-16.
    using fem::app::LoadKind;
    for (const LoadKind kind : fem::app::kLoadKinds) {
        const s::Action action = fem::app::newAction(kind, s::UnitSystem::KilonewtonMetre);
        CHECK(static_cast<std::size_t>(action.index()) == static_cast<std::size_t>(kind));
        CHECK(fem::app::loadOnNodes(kind) ==
              (kind == LoadKind::Nodal || kind == LoadKind::Settlement));
    }
    // Gravity along -Y, in the units of the model.
    const auto mm = fem::app::newAction(LoadKind::SelfWeight, s::UnitSystem::NewtonMillimetre);
    CHECK(std::abs(std::get<s::SelfWeight>(mm).acceleration.y() + 9806.65) <= 1e-9);
    CHECK(std::get<s::SelfWeight>(mm).acceleration.x() == 0.0);
}

TEST_CASE("the terms of a combination from the factors typed", "[app][fields]")
{
    // Feature 010, T19: REQ-010-17.
    const std::vector<std::pair<s::ItemId, QString>> typed{{s::ItemId{1}, QStringLiteral("1.4")},
                                                           {s::ItemId{2}, QString()},
                                                           {s::ItemId{3}, QStringLiteral(" 1.5 ")},
                                                           {s::ItemId{4}, QStringLiteral("0")}};
    const auto terms = fem::app::termsOf(typed);
    REQUIRE(terms.has_value());
    CHECK(terms.value_or(std::vector<std::pair<s::ItemId, double>>{}) ==
          std::vector<std::pair<s::ItemId, double>>{{s::ItemId{1}, 1.4}, {s::ItemId{3}, 1.5}});
    const std::vector<std::pair<s::ItemId, QString>> wrong{{s::ItemId{1}, QStringLiteral("x")}};
    CHECK_FALSE(fem::app::termsOf(wrong).has_value());
}
