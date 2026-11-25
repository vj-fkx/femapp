#pragma once

// The 2 x 2 frame of 009 T10 drawn and prepared in a document, for the tests
// of features 010 and 011.

#include <fem/core/model.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace fem::setup::test {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::EntityId;
using fem::geom::Vec3;

inline EntityId addLine(Drawing& drawing, const Vec3& a, const Vec3& b)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = fem::geom::LineEntity{.start = a, .end = b}});
    drawing.apply(edit);
    return id;
}

inline EntityId addPoint(Drawing& drawing, const Vec3& p)
{
    const EntityId id = drawing.nextId();
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = id, .shape = fem::geom::PointEntity{.position = p}});
    drawing.apply(edit);
    return id;
}

inline s::ItemId added(s::Document& document, s::EditResult result)
{
    REQUIRE(result.has_value());
    document.apply(*result);
    return s::idOf(result->added.back());
}

// The 2 x 2 frame of 009 T10: columns, beams, points at the three bases and
// at the top corner.
struct Frame
{
    Drawing drawing;
    std::vector<EntityId> bars;
    std::vector<EntityId> bases;
    EntityId top{};
};

inline void drawFrame(Frame& f)
{
    for (int col = 0; col < 3; ++col) {
        for (int st = 0; st < 2; ++st) {
            f.bars.push_back(addLine(f.drawing, Vec3(4.0 * col, 3.0 * st, 0.0),
                                     Vec3(4.0 * col, 3.0 * (st + 1), 0.0)));
        }
    }
    for (int st = 1; st <= 2; ++st) {
        for (int b = 0; b < 2; ++b) {
            f.bars.push_back(addLine(f.drawing, Vec3(4.0 * b, 3.0 * st, 0.0),
                                     Vec3(4.0 * (b + 1), 3.0 * st, 0.0)));
        }
    }
    for (int col = 0; col < 3; ++col) {
        f.bases.push_back(addPoint(f.drawing, Vec3(4.0 * col, 0.0, 0.0)));
    }
    f.top = addPoint(f.drawing, Vec3(0.0, 6.0, 0.0));
}

inline s::MeshContext meshOf(const Drawing& drawing)
{
    auto meshed = fem::mesher::meshLines(drawing);
    REQUIRE(meshed.has_value());
    s::MeshContext context;
    context.mesh = std::make_shared<const c::Mesh>(std::move(meshed->mesh));
    context.map = std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map));
    context.generation = 1;
    context.drawingRevision = drawing.revision();
    return context;
}

inline s::Targets entities(const std::vector<EntityId>& ids)
{
    s::Targets t;
    t.entities = ids;
    return t;
}

// The document of the frame: steel, a rectangle, beams on every bar, the
// bases fixed and a load at the top corner.
struct Setup
{
    s::Document document;
    s::ItemId loadCase{};
};

inline void prepare(Setup& setup, const Frame& f)
{
    s::Document& d = setup.document;
    added(d,
          s::addItem(d, s::MaterialItem{
                            .id = {},
                            .data = {.name = "steel", .youngsModulus = 2.0e8, .poissonsRatio = 0.3},
                            .standard = {},
                            .grade = {},
                            .properties = {}}));
    added(d, s::addItem(d, s::SectionItem{.id = {},
                                          .data = {.name = "bar",
                                                   .shape = c::Rectangle{.b = 0.3, .h = 0.5}}}));
    added(d, s::assign(d, s::BarAssignment{.id = {},
                                           .targets = entities(f.bars),
                                           .data = {.elements = {},
                                                    .kind = c::ElementKind::Beam2D,
                                                    .material = "steel",
                                                    .section = "bar"}}));
    s::SupportItem fixed{.id = {}, .targets = entities(f.bases), .held = {}};
    fixed.held[static_cast<std::size_t>(c::DofType::U)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::V)] = true;
    fixed.held[static_cast<std::size_t>(c::DofType::RZ)] = true;
    added(d, s::assign(d, fixed));
    setup.loadCase = added(
        d, s::addItem(d, s::LoadCase{.id = {}, .name = "lateral", .standard = {}, .category = {}}));
    added(d, s::addItem(d, s::LoadItem{.id = {},
                                       .loadCase = setup.loadCase,
                                       .targets = entities({f.top}),
                                       .action = c::NodalForce{.nodes = {},
                                                               .force = Vec3(10.0, -20.0, 0.0)}}));
}

} // namespace fem::setup::test
