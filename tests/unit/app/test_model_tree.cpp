#include "model_controller.hpp"
#include "model_tree.hpp"

#include <QUndoStack>

#include <catch2/catch_test_macros.hpp>

#include <string>

// Feature 010, T14 (design §5.5): REQ-010-13, 14, 18, 19. The rows of the
// model tree over a document (without a view: no fonts or icons).

namespace {

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
    CHECK(tree.indexOf(*id) == bars);
    CHECK_FALSE(tree.itemAt(materials).has_value());
    CHECK(tree.groupAt(bars) == Group::Bars);

    // Undo takes the rows back.
    undo.undo();
    CHECK(tree.rowCount(groupIndex(tree, Group::Bars)) == 0);
}
