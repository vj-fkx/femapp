#include "model_controller.hpp"
#include "model_tree.hpp"

#include <QUndoStack>

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

// Feature 010, T14 (design §5.5): REQ-010-13, 14, 18, 19. The rows of the
// model tree over a document (without a view: no fonts or icons).

namespace {

// The value of an optional the test requires: REQUIRE, then value_or, so
// that static analysis sees the check (clang-tidy 19 on Linux).
template <typename T>
T required(const std::optional<T>& value)
{
    REQUIRE(value.has_value());
    return value.value_or(T{});
}

namespace s = fem::setup;
using fem::app::ModelTree;
using Group = ModelTree::Group;

std::string textOf(const QModelIndex& index)
{
    return index.data(Qt::DisplayRole).toString().toStdString();
}

QModelIndex groupIndex(const ModelTree& tree, Group group)
{
    return tree.index(static_cast<int>(group), 0);
}

} // namespace

TEST_CASE("the rows of the model tree", "[app][model][tree]")
{
    const fem::geom::Drawing drawing;
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    const ModelTree tree(&controller);

    // Nine groups, the units and the mesh with one row of text.
    REQUIRE(tree.rowCount() == 9);
    CHECK(textOf(groupIndex(tree, Group::Materials)) == "Materials");
    CHECK(textOf(groupIndex(tree, Group::LoadCases)) == "Load cases");
    REQUIRE(tree.rowCount(groupIndex(tree, Group::Units)) == 1);
    CHECK(textOf(tree.index(0, 0, groupIndex(tree, Group::Units))) == "kN, m, t, s, °C");
    CHECK(textOf(tree.index(0, 0, groupIndex(tree, Group::Mesh))) == "No mesh (run MESH)");

    // Items appear in their groups as the document changes.
    s::Document& ids = controller.documentForIds();
    controller.push(s::addItem(ids, s::MaterialItem{.id = {},
                                                    .data = {.name = "steel"},
                                                    .standard = {},
                                                    .grade = {},
                                                    .properties = {}}),
                    QStringLiteral("material"));
    controller.push(
        s::addItem(
            ids, s::LoadCase{.id = {}, .name = "Office", .standard = "EN 1990", .category = "Q-B"}),
        QStringLiteral("case"));
    const s::ItemId office = s::idOf(controller.document().items().back());
    s::Targets beam;
    beam.entities = {fem::geom::EntityId{7}}; // not in the drawing
    controller.push(s::addItem(ids, s::LoadItem{.id = {},
                                                .loadCase = office,
                                                .targets = beam,
                                                .action = fem::core::NodalForce{}}),
                    QStringLiteral("load"));
    controller.push(s::assign(ids, s::BarAssignment{.id = {},
                                                    .targets = beam,
                                                    .data = {.elements = {},
                                                             .kind = fem::core::ElementKind::Beam2D,
                                                             .material = "steel",
                                                             .section = "IPE"}}),
                    QStringLiteral("bars"));

    const QModelIndex materials = groupIndex(tree, Group::Materials);
    REQUIRE(tree.rowCount(materials) == 1);
    CHECK(textOf(tree.index(0, 0, materials)) == "steel");

    // The case with its classification, active, and its load under it.
    const QModelIndex cases = groupIndex(tree, Group::LoadCases);
    REQUIRE(tree.rowCount(cases) == 1);
    const QModelIndex caseRow = tree.index(0, 0, cases);
    CHECK(textOf(caseRow) == "Office [EN 1990 Q-B]");
    CHECK(caseRow.data(ModelTree::ActiveRole).toBool());
    REQUIRE(tree.rowCount(caseRow) == 1);
    CHECK(textOf(tree.index(0, 0, caseRow)) == "Nodal force (1 target(s))");
    CHECK(tree.parent(tree.index(0, 0, caseRow)) == caseRow);

    // The bars, with targets that are not in the drawing (REQ-010-14).
    const QModelIndex bars = tree.index(0, 0, groupIndex(tree, Group::Bars));
    CHECK(textOf(bars) == "Beam 2D: steel, IPE (1 target(s))");
    CHECK(bars.data(ModelTree::StateRole).toInt() ==
          static_cast<int>(s::TargetState::MissingEntity));
    CHECK_FALSE(bars.data(Qt::ToolTipRole).toString().isEmpty());

    // Items are found by id, and rows give their item.
    const auto id = tree.itemAt(bars);
    REQUIRE(id.has_value());
    CHECK(tree.indexOf(required(id)) == bars);
    CHECK_FALSE(tree.itemAt(materials).has_value());
    CHECK(tree.groupAt(bars) == Group::Bars);

    // Undo takes the rows back.
    undo.undo();
    CHECK(tree.rowCount(groupIndex(tree, Group::Bars)) == 0);
}

// Feature 011, T16 (design §5.5): REQ-011-16. The cases and combinations
// with a result say its type and whether it is current.
TEST_CASE("the state of the results in the model tree", "[app][model][tree][analysis]")
{
    const fem::geom::Drawing drawing;
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    const ModelTree tree(&controller);
    s::Document& ids = controller.documentForIds();
    controller.push(
        s::addItem(ids, s::LoadCase{.id = {}, .name = "G", .standard = {}, .category = {}}),
        QStringLiteral("case"));
    const s::ItemId dead = s::idOf(controller.document().items().back());
    controller.push(
        s::addItem(ids, s::Combination{.id = {}, .name = "ULS", .terms = {{dead, 1.35}}}),
        QStringLiteral("combination"));
    const s::ItemId uls = s::idOf(controller.document().items().back());
    const QModelIndex caseRow = tree.index(0, 0, groupIndex(tree, Group::LoadCases));
    const QModelIndex combinationRow = tree.index(0, 0, groupIndex(tree, Group::Combinations));
    CHECK(textOf(caseRow) == "G");
    CHECK(textOf(combinationRow) == "ULS = 1.35 G");

    // A result of the state now: current. The rows are told, not rebuilt.
    int changed = 0;
    QObject::connect(&tree, &QAbstractItemModel::dataChanged, [&changed] { ++changed; });
    const auto finish = [&controller](s::ItemId item, s::AnalysisType type) {
        s::AnalysisResult result;
        result.item = item;
        result.settings.type = type;
        result.state = controller.state();
        controller.caseFinished(
            s::CaseOutcome{.result = std::move(result), .issues = {}, .error = std::nullopt});
    };
    finish(dead, s::AnalysisType::Linear);
    CHECK(changed == 2); // the cases and the combinations
    CHECK(textOf(caseRow) == "G (linear · current)");
    CHECK(textOf(combinationRow) == "ULS = 1.35 G");
    finish(uls, s::AnalysisType::Buckling);
    CHECK(textOf(combinationRow) == "ULS = 1.35 G (buckling · current)");

    // An edit of the document puts them out of date; undone, they are
    // current again (REQ-011-12).
    controller.push(
        s::addItem(ids, s::LoadCase{.id = {}, .name = "Q", .standard = {}, .category = {}}),
        QStringLiteral("case"));
    const QModelIndex cases = groupIndex(tree, Group::LoadCases);
    REQUIRE(tree.rowCount(cases) == 2);
    CHECK(textOf(tree.index(0, 0, cases)) == "G (linear · out of date)");
    CHECK(textOf(tree.index(1, 0, cases)) == "Q");
    CHECK(textOf(tree.index(0, 0, groupIndex(tree, Group::Combinations))) ==
          "ULS = 1.35 G (buckling · out of date)");
    undo.undo();
    CHECK(textOf(tree.index(0, 0, groupIndex(tree, Group::LoadCases))) == "G (linear · current)");
}
