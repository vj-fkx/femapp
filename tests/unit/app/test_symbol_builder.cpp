#include "symbol_builder.hpp"

#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <vector>

// Feature 010, T21 (design §5.6): REQ-010-09, 22. The symbols of the
// active case, on the drawing before MESH and on the mesh after it.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::Drawing;
using fem::geom::EntityId;
using fem::geom::Vec3;

EntityId addEntity(Drawing& drawing, fem::geom::EntityShape shape)
{
    const EntityId id = drawing.nextId();
    fem::geom::DrawingEdit edit;
    edit.added.push_back(fem::geom::Entity{.id = id, .shape = std::move(shape)});
    drawing.apply(edit);
    return id;
}

s::ItemId added(s::Document& document, const s::EditResult& edit)
{
    REQUIRE(edit.has_value());
    document.apply(*edit);
    return s::idOf(edit->added.empty() ? s::Item{} : edit->added.front());
}

s::Targets on(std::vector<EntityId> ids)
{
    s::Targets t;
    t.entities = std::move(ids);
    return t;
}

// A column and a beam, pinned at the base, a force at the top and a load
// along the beam.
struct Portal
{
    Drawing drawing;
    EntityId column{};
    EntityId beam{};
    EntityId base{};
    EntityId top{};
    s::Document document;
    s::ItemId loadCase{};
};

void build(Portal& p)
{
    p.column =
        addEntity(p.drawing, fem::geom::LineEntity{.start = Vec3(0, 0, 0), .end = Vec3(0, 3, 0)});
    p.beam =
        addEntity(p.drawing, fem::geom::LineEntity{.start = Vec3(0, 3, 0), .end = Vec3(4, 3, 0)});
    p.base = addEntity(p.drawing, fem::geom::PointEntity{.position = Vec3(0, 0, 0)});
    p.top = addEntity(p.drawing, fem::geom::PointEntity{.position = Vec3(0, 3, 0)});
    s::Document& d = p.document;
    // The model built on the mesh keeps only assignments it can analyse.
    added(d, s::addItem(
                 d, s::MaterialItem{
                        .id = {},
                        .data = {.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3}}));
    added(d, s::addItem(d, s::SectionItem{.id = {},
                                          .data = {.name = "bar",
                                                   .shape = c::Rectangle{.b = 0.3, .h = 0.5}}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = on({p.column, p.beam}),
                                           .data = {.kind = c::ElementKind::Beam2D,
                                                    .material = "steel",
                                                    .section = "bar"}}));
    s::SupportItem pinned{.id = {}, .targets = on({p.base}), .held = {}};
    pinned.held[static_cast<std::size_t>(c::DofType::U)] = true;
    pinned.held[static_cast<std::size_t>(c::DofType::V)] = true;
    added(d, s::assign(d, pinned));
    p.loadCase = added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "live"}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = p.loadCase,
                                       .targets = on({p.top}),
                                       .action = c::NodalForce{.force = Vec3(5, 0, 0)}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = p.loadCase,
                                       .targets = on({p.beam}),
                                       .action = c::DistributedLoad{.startValue = Vec3(0, -2, 0),
                                                                    .endValue = Vec3(0, -2, 0)}}));
}

double lengthOf(const fem::render::SymbolSet& set)
{
    double total = 0.0;
    for (const auto& l : set.distributed) {
        total += (l.to - l.from).norm();
    }
    return total;
}

} // namespace

TEST_CASE("the symbols of a model before MESH", "[app][symbols]")
{
    Portal p;
    build(p);
    const auto set = fem::app::buildSymbols(p.document, p.drawing, {}, {});
    REQUIRE(set.supports.size() == 1);
    CHECK(set.supports.front().at == Vec3(0, 0, 0));
    CHECK(set.supports.front().planar);
    CHECK(fem::render::supportShape(set.supports.front().held, true).glyph ==
          fem::render::SupportGlyph::Pinned);
    REQUIRE(set.forces.size() == 1);
    CHECK(set.forces.front().vector == Vec3(5, 0, 0));
    REQUIRE(set.distributed.size() == 1);
    CHECK(std::abs(lengthOf(set) - 4.0) <= 1e-12);
    CHECK(set.axes.empty());

    // REQ-010-09: the axes of all the beams, or of the selected one.
    const auto all = fem::app::buildSymbols(p.document, p.drawing, {},
                                            {.axes = fem::app::AxesShown::All, .axesOf = {}});
    CHECK(all.axes.size() == 2);
    const auto one = fem::app::buildSymbols(
        p.document, p.drawing, {}, {.axes = fem::app::AxesShown::Selected, .axesOf = on({p.beam})});
    REQUIRE(one.axes.size() == 1);
    CHECK(one.axes.front().at == Vec3(2, 3, 0));
    CHECK((one.axes.front().axes.col(0) - Vec3::UnitX()).norm() <= 1e-12);
}

TEST_CASE("the symbols of a model on its mesh, for a combination", "[app][symbols]")
{
    Portal p;
    build(p);
    auto meshed = fem::mesher::meshLines(p.drawing);
    REQUIRE(meshed.has_value());
    s::MeshContext mesh;
    mesh.mesh = std::make_shared<const c::Mesh>(std::move(meshed->mesh));
    mesh.map = std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map));
    mesh.generation = 1;
    mesh.drawingRevision = p.drawing.revision();

    const auto set = fem::app::buildSymbols(p.document, p.drawing, mesh,
                                            {.axes = fem::app::AxesShown::All, .axesOf = {}});
    REQUIRE(set.supports.size() == 1);
    CHECK(set.supports.front().at == Vec3(0, 0, 0));
    REQUIRE(set.forces.size() == 1);
    CHECK(set.forces.front().at == Vec3(0, 3, 0));
    // One distributed load per element of the beam, covering it.
    CHECK_FALSE(set.distributed.empty());
    CHECK(std::abs(lengthOf(set) - 4.0) <= 1e-9);
    std::size_t bars = 0;
    for (int e = 0; e < mesh.mesh->elementCount(); ++e) {
        bars += mesh.mesh->elementType(e) == c::ElementType::Line2 ||
                        mesh.mesh->elementType(e) == c::ElementType::Line3
                    ? 1
                    : 0;
    }
    CHECK(set.axes.size() == bars);

    // A combination: 1.5 times the case.
    s::Document& d = p.document;
    const s::ItemId combination = added(
        d, s::addItem(d, s::Combination{.id = {}, .name = "ULS", .terms = {{p.loadCase, 1.5}}}));
    added(d, s::setActiveCase(d, combination));
    const auto factored = fem::app::buildSymbols(d, p.drawing, mesh, {});
    REQUIRE(factored.forces.size() == 1);
    CHECK((factored.forces.front().vector - Vec3(7.5, 0, 0)).norm() <= 1e-12);
    const auto beforeMesh = fem::app::buildSymbols(d, p.drawing, {}, {});
    REQUIRE(beforeMesh.distributed.size() == 1);
    CHECK((beforeMesh.distributed.front().startValue - Vec3(0, -3, 0)).norm() <= 1e-12);
}
