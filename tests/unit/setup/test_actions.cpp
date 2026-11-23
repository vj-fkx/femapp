#include <fem/setup/actions.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// Feature 010, T10b (design §5.10): REQ-010-26. The table of the design,
// written here again on its own.

namespace {

namespace s = fem::setup;
using s::ActionNature;

struct Row
{
    const char* standard;
    const char* code;
    ActionNature nature;
    std::optional<double> psi0;
    std::optional<double> psi1;
    std::optional<double> psi2;
};

Row row(const char* standard, const char* code, ActionNature nature)
{
    return {
        .standard = standard, .code = code, .nature = nature, .psi0 = {}, .psi1 = {}, .psi2 = {}};
}

Row row(const char* standard, const char* code, double psi0, double psi1, double psi2)
{
    return {.standard = standard,
            .code = code,
            .nature = ActionNature::Variable,
            .psi0 = psi0,
            .psi1 = psi1,
            .psi2 = psi2};
}

const std::vector<Row>& expected()
{
    using N = ActionNature;
    static const std::vector<Row> rows{
        row("ABNT NBR 8681", "g1", N::Permanent),  row("ABNT NBR 8681", "g2", N::Permanent),
        row("ABNT NBR 8681", "ε", N::Permanent),   row("ABNT NBR 8681", "q1", 0.5, 0.4, 0.3),
        row("ABNT NBR 8681", "q2", 0.7, 0.6, 0.4), row("ABNT NBR 8681", "q3", 0.8, 0.7, 0.6),
        row("ABNT NBR 8681", "v", 0.6, 0.3, 0.0),  row("ABNT NBR 8681", "t", 0.6, 0.5, 0.3),
        row("ABNT NBR 8681", "e", N::Accidental),  row("ABNT NBR 8681", "s", N::Seismic),
        row("EN 1990", "G", N::Permanent),         row("EN 1990", "P", N::Permanent),
        row("EN 1990", "Q-A", 0.7, 0.5, 0.3),      row("EN 1990", "Q-B", 0.7, 0.5, 0.3),
        row("EN 1990", "Q-C", 0.7, 0.7, 0.6),      row("EN 1990", "Q-D", 0.7, 0.7, 0.6),
        row("EN 1990", "Q-E", 1.0, 0.9, 0.8),      row("EN 1990", "Q-F", 0.7, 0.7, 0.6),
        row("EN 1990", "Q-G", 0.7, 0.5, 0.3),      row("EN 1990", "Q-H", 0.0, 0.0, 0.0),
        row("EN 1990", "Q-S", 0.5, 0.2, 0.0),      row("EN 1990", "Q-W", 0.6, 0.2, 0.0),
        row("EN 1990", "Q-T", 0.6, 0.5, 0.0),      row("EN 1990", "A", N::Accidental),
        row("EN 1990", "AE", N::Seismic),          row("ASCE 7", "D", N::Permanent),
        row("ASCE 7", "L", N::Variable),           row("ASCE 7", "Lr", N::Variable),
        row("ASCE 7", "S", N::Variable),           row("ASCE 7", "R", N::Variable),
        row("ASCE 7", "W", N::Variable),           row("ASCE 7", "E", N::Seismic),
        row("ASCE 7", "H", N::Permanent),          row("ASCE 7", "F", N::Permanent),
        row("ASCE 7", "T", N::Variable),
    };
    return rows;
}

} // namespace

TEST_CASE("the categories of actions follow the table", "[setup][actions]")
{
    const auto& categories = s::actionCategories();
    REQUIRE(categories.size() == expected().size());
    for (std::size_t k = 0; k < categories.size(); ++k) {
        const s::ActionCategory& c = categories[k];
        const Row& r = expected()[k];
        INFO(r.standard << " " << r.code);
        CHECK(c.standard == r.standard);
        CHECK(c.code == r.code);
        CHECK(c.nature == r.nature);
        CHECK(c.psi0 == r.psi0);
        CHECK(c.psi1 == r.psi1);
        CHECK(c.psi2 == r.psi2);
        CHECK_FALSE(c.name.empty());
    }
    // Codes are unique within a standard.
    std::map<std::pair<std::string, std::string>, int> seen;
    for (const s::ActionCategory& c : categories) {
        CHECK(++seen[{c.standard, c.code}] == 1);
    }
}

TEST_CASE("a category is found by standard and code", "[setup][actions]")
{
    const s::ActionCategory* wind = s::findCategory("EN 1990", "Q-W");
    REQUIRE(wind != nullptr);
    CHECK(wind->psi0 == 0.6);
    CHECK(s::findCategory("ABNT NBR 8681", "v") != nullptr);
    CHECK(s::findCategory("ASCE 7", "W") != nullptr);
    // The same code in another standard is another category.
    CHECK(s::findCategory("ASCE 7", "Q-W") == nullptr);
    CHECK(s::findCategory("EN 1990", "nope") == nullptr);
}

// The classification goes with the load case through the edits.
TEST_CASE("a load case keeps its classification", "[setup][actions]")
{
    s::Document document;
    const auto add = s::addItem(
        document,
        s::LoadCase{.id = {}, .name = "Office", .standard = "EN 1990", .category = "Q-B"});
    REQUIRE(add.has_value());
    document.apply(*add);
    const s::ItemId id = s::idOf(add->added.back());
    const auto& made = std::get<s::LoadCase>(*document.find(id));
    CHECK(made.standard == "EN 1990");
    CHECK(made.category == "Q-B");

    s::LoadCase changed = made;
    changed.standard = "ABNT NBR 8681";
    changed.category = "q2";
    const auto change = s::changeItem(document, changed);
    REQUIRE(change.has_value());
    document.apply(*change);
    CHECK(std::get<s::LoadCase>(*document.find(id)).category == "q2");

    const auto copy = s::duplicateItem(document, id);
    REQUIRE(copy.has_value());
    document.apply(*copy);
    const auto& duplicated = std::get<s::LoadCase>(document.items().back());
    CHECK(duplicated.standard == "ABNT NBR 8681");
    CHECK(duplicated.category == "q2");
}
