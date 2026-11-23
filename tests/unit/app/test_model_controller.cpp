#include "model_controller.hpp"

#include <QUndoStack>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <variant>
#include <vector>

// Feature 010, T13 (design §5.8): REQ-010-13, 23. The document edits on the
// undo stack of the drawing, and the generations of the mesh.

namespace {

namespace s = fem::setup;
using fem::geom::Drawing;
using fem::geom::Vec3;

std::shared_ptr<const fem::render::MeshScene> sceneOf(const Drawing& drawing)
{
    auto meshed = fem::mesher::meshLines(drawing);
    REQUIRE(meshed.has_value());
    return fem::render::buildMeshScene(
        std::make_shared<const fem::core::Mesh>(std::move(meshed->mesh)));
}

} // namespace

TEST_CASE("document edits go through the undo stack", "[app][model]")
{
    const Drawing drawing;
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    int changes = 0;
    QObject::connect(&controller, &fem::app::ModelController::documentChanged,
                     [&changes] { ++changes; });
    int refusals = 0;
    QObject::connect(&controller, &fem::app::ModelController::refused,
                     [&refusals](const s::EditRefusal&) { ++refusals; });

    s::Document& ids = controller.documentForIds();
    CHECK(controller.push(s::addItem(ids, s::MaterialItem{.id = {}, .data = {.name = "steel"}}),
                          QStringLiteral("material")));
    CHECK(controller.document().size() == 1);
    CHECK(changes == 1);
    CHECK(undo.count() == 1);
    // A refused edit changes nothing and is not on the stack.
    CHECK_FALSE(
        controller.push(s::addItem(ids, s::MaterialItem{.id = {}, .data = {.name = "steel"}}),
                        QStringLiteral("again")));
    CHECK(refusals == 1);
    CHECK(undo.count() == 1);
    undo.undo();
    CHECK(controller.document().size() == 0);
    undo.redo();
    CHECK(controller.document().size() == 1);
    CHECK(changes == 3);
    // An empty edit is not pushed.
    controller.push(s::changeUnits(controller.document(), controller.document().units()),
                    QStringLiteral("nothing"));
    CHECK(undo.count() == 1);
}

TEST_CASE("every new mesh is a new generation", "[app][model]")
{
    Drawing drawing;
    fem::geom::DrawingEdit edit;
    edit.added.push_back(fem::geom::Entity{
        .id = drawing.nextId(),
        .shape = fem::geom::LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(1, 0, 0)}});
    drawing.apply(edit);
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    CHECK(controller.mesh().mesh == nullptr);
    const auto scene = sceneOf(drawing);
    auto map = std::make_shared<const fem::mesher::LineMeshMap>();
    controller.meshGenerated(scene, map, drawing.revision());
    CHECK(controller.mesh().mesh == scene->sharedMesh());
    CHECK(controller.mesh().map == map);
    CHECK(controller.mesh().generation == 1);
    CHECK(controller.mesh().drawingRevision == drawing.revision());
    // An imported mesh: no map, no revision, the next generation.
    controller.meshImported(scene);
    CHECK(controller.mesh().map == nullptr);
    CHECK_FALSE(controller.mesh().drawingRevision.has_value());
    CHECK(controller.mesh().generation == 2);
}

TEST_CASE("a solid polygon section from closed polylines", "[app][model][section]")
{
    // Feature 010, T16: REQ-010-05.
    Drawing drawing;
    const auto add = [&](std::vector<Vec3> vertices) {
        const fem::geom::EntityId id = drawing.nextId();
        fem::geom::DrawingEdit edit;
        edit.added.push_back(fem::geom::Entity{
            .id = id,
            .shape = fem::geom::PolylineEntity{.vertices = std::move(vertices), .closed = true}});
        drawing.apply(edit);
        return id;
    };
    const auto outer = add({{0, 0, 0}, {0.4, 0, 0}, {0.4, 0.3, 0}, {0, 0.3, 0}});
    const auto hole = add({{0.1, 0.1, 0}, {0.2, 0.1, 0}, {0.2, 0.2, 0}, {0.1, 0.2, 0}});
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    s::Document& ids = controller.documentForIds();
    REQUIRE(controller.push(
        s::addItem(ids, s::SectionItem{.id = {}, .data = {.name = "slab", .area = 1.0}}),
        QStringLiteral("section")));
    const s::ItemId section = std::get<s::SectionItem>(controller.document().items().front()).id;
    const std::array<fem::geom::EntityId, 2> both{hole, outer};
    CHECK_FALSE(controller.setSolidFromPolylines(section, both).has_value());
    CHECK(undo.count() == 2);
    const auto& made = std::get<s::SectionItem>(*controller.document().find(section));
    CHECK(made.data.name == "slab");
    CHECK_FALSE(made.data.area.has_value());
    REQUIRE(made.data.shape.has_value());
    const auto& solid = std::get<fem::core::SolidPolygon>(*made.data.shape);
    CHECK(solid.regions.front().holes.size() == 1);
    // One step of undo brings the generic section back.
    undo.undo();
    CHECK(std::get<s::SectionItem>(*controller.document().find(section)).data.area == 1.0);
    // Not a section, nothing selected: a text, and no edit.
    CHECK(controller.setSolidFromPolylines(s::ItemId{999}, both).has_value());
    CHECK(controller.setSolidFromPolylines(section, {}).has_value());
    CHECK(undo.count() == 2);
}

TEST_CASE("the targets of an assignment from the viewport selection", "[app][model]")
{
    // Feature 010, T17: REQ-010-12.
    Drawing drawing;
    fem::geom::DrawingEdit edit;
    const fem::geom::EntityId line = drawing.nextId();
    edit.added.push_back(fem::geom::Entity{
        .id = line, .shape = fem::geom::LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(2, 0, 0)}});
    drawing.apply(edit);
    QUndoStack undo;
    fem::app::ModelController controller(&undo, drawing);
    const auto scene = sceneOf(drawing);
    controller.meshGenerated(scene, nullptr, drawing.revision());
    const fem::core::Mesh& mesh = *scene->sharedMesh();

    // Entities of the drawing win over the mesh.
    fem::render::SelectionItems picked;
    picked[fem::render::SelectionKind::Element] = {0};
    picked[fem::render::SelectionKind::Node] = {1};
    const std::array<fem::geom::EntityId, 1> entities{line};
    const s::Targets fromDrawing = controller.targetsFrom(entities, picked);
    CHECK(fromDrawing.entities == std::vector<fem::geom::EntityId>{line});
    CHECK(fromDrawing.elements.empty());

    // Picked in the mesh: by the external ids, in this generation.
    const s::Targets fromMesh = controller.targetsFrom({}, picked);
    CHECK(fromMesh.entities.empty());
    CHECK(fromMesh.elements == std::vector<fem::core::ExternalId>{mesh.elementId(0)});
    CHECK(fromMesh.nodes == std::vector<fem::core::ExternalId>{mesh.nodeId(1)});
    CHECK(fromMesh.meshGeneration == controller.mesh().generation);

    // The groups of the mesh, by dimension.
    std::size_t lines = 0;
    for (const fem::core::Group& g : mesh.groups()) {
        lines += g.dimension == 1 ? 1 : 0;
    }
    CHECK(controller.meshGroups(1).size() == lines);
    CHECK(controller.meshGroups(3).empty());
}
