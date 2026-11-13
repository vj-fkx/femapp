#include <fem/geom/drawing.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <variant>
#include <vector>

using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::EntityId;
using fem::geom::LineEntity;
using fem::geom::PointEntity;
using fem::geom::PolylineEntity;
using fem::geom::Vec3;

namespace {

Entity line(EntityId id, const Vec3& a, const Vec3& b)
{
    return {.id = id, .shape = LineEntity{.start = a, .end = b}};
}

std::uint64_t value(EntityId id)
{
    return static_cast<std::uint64_t>(id);
}

const LineEntity& asLine(const Entity* entity)
{
    REQUIRE(entity != nullptr);
    const auto* shape = std::get_if<LineEntity>(&entity->shape);
    REQUIRE(shape != nullptr);
    return *shape;
}

} // namespace

// REQ-005-01, REQ-005-02
TEST_CASE("drawing inserts, finds and removes entities with stable ids", "[geom][drawing]")
{
    Drawing drawing;
    const EntityId a = drawing.nextId();
    const EntityId b = drawing.nextId();
    CHECK(value(a) == 1);
    CHECK(value(b) == 2);

    const auto before = drawing.revision();
    drawing.apply({.removed = {},
                   .added = {line(a, Vec3(0, 0, 0), Vec3(1, 0, 0)),
                             {.id = b, .shape = PointEntity{.position = Vec3(5, 5, 5)}}}});
    CHECK(drawing.size() == 2);
    CHECK(drawing.revision() > before);
    CHECK(asLine(drawing.find(a)).end == Vec3(1, 0, 0));

    // Removing the first keeps the second reachable.
    drawing.apply({.removed = {*drawing.find(a)}, .added = {}});
    CHECK(drawing.find(a) == nullptr);
    REQUIRE(drawing.find(b) != nullptr);
    CHECK(drawing.find(b)->id == b);

    // Ids are never reused.
    CHECK(value(drawing.nextId()) == 3);
}

// REQ-005-31, REQ-005-32: apply and revert of create, move and erase.
TEST_CASE("edits are reversible and restore the ids", "[geom][drawing]")
{
    Drawing drawing;
    const EntityId id = drawing.nextId();
    const Entity original = line(id, Vec3(0, 0, 0), Vec3(2, 0, 0));

    const DrawingEdit create{.removed = {}, .added = {original}};
    drawing.apply(create);

    const Entity moved = fem::geom::translated(original, Vec3(0, 1, 0), id);
    const DrawingEdit move{.removed = {original}, .added = {moved}};
    drawing.apply(move);
    CHECK(asLine(drawing.find(id)).start == Vec3(0, 1, 0));

    const DrawingEdit erase{.removed = {moved}, .added = {}};
    drawing.apply(erase);
    CHECK(drawing.empty());

    drawing.revert(erase);
    CHECK(asLine(drawing.find(id)).start == Vec3(0, 1, 0));
    drawing.revert(move);
    CHECK(asLine(drawing.find(id)).start == Vec3(0, 0, 0));
    drawing.revert(create);
    CHECK(drawing.empty());

    // Redo brings the same id back, and new ids keep growing.
    drawing.apply(create);
    REQUIRE(drawing.find(id) != nullptr);
    CHECK(value(drawing.nextId()) == 2);
}

// REQ-005-31: a command made of several steps is one edit.
TEST_CASE("edits are folded into one", "[geom][drawing]")
{
    const EntityId a{1};
    const EntityId b{2};
    const Entity a0 = line(a, Vec3(0, 0, 0), Vec3(1, 0, 0));
    const Entity a1 = fem::geom::translated(a0, Vec3(1, 0, 0), a);
    const Entity a2 = fem::geom::translated(a1, Vec3(1, 0, 0), a);
    const Entity b0 = line(b, Vec3(0, 0, 0), Vec3(0, 1, 0));

    // a existed before; moved twice. b created, then erased.
    DrawingEdit total{.removed = {a0}, .added = {a1}};
    total.append({.removed = {a1}, .added = {a2}});
    total.append({.removed = {}, .added = {b0}});
    total.append({.removed = {b0}, .added = {}});

    REQUIRE(total.removed.size() == 1);
    CHECK(asLine(&total.removed[0]).start == Vec3(0, 0, 0));
    REQUIRE(total.added.size() == 1);
    CHECK(asLine(&total.added[0]).start == Vec3(2, 0, 0));
    CHECK_FALSE(total.empty());
    CHECK(DrawingEdit{}.empty());
}

// REQ-005-01
TEST_CASE("segments, vertices and translation of each entity", "[geom][drawing]")
{
    const EntityId id{7};
    const Entity point{.id = id, .shape = PointEntity{.position = Vec3(1, 2, 3)}};
    const Entity open{.id = id,
                      .shape =
                          PolylineEntity{.vertices = {Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0)},
                                         .closed = false}};
    Entity closed = open;
    std::get<PolylineEntity>(closed.shape).closed = true;

    CHECK(fem::geom::segmentsOf(point).empty());
    CHECK(fem::geom::segmentsOf(line(id, Vec3::Zero(), Vec3::UnitX())).size() == 1);
    CHECK(fem::geom::segmentsOf(open).size() == 2);
    const auto loop = fem::geom::segmentsOf(closed);
    REQUIRE(loop.size() == 3);
    CHECK(loop[2].a == Vec3(1, 1, 0));
    CHECK(loop[2].b == Vec3(0, 0, 0));
    CHECK(loop[2].entity == id);

    CHECK(fem::geom::verticesOf(point) == std::vector<Vec3>{Vec3(1, 2, 3)});
    const Entity shifted = fem::geom::translated(open, Vec3(0, 0, 2), EntityId{9});
    CHECK(shifted.id == EntityId{9});
    CHECK(fem::geom::verticesOf(shifted)[2] == Vec3(1, 1, 2));
}

TEST_CASE("bounds and tolerance follow the drawing size", "[geom][drawing]")
{
    Drawing drawing;
    CHECK_FALSE(drawing.bounds().has_value());
    CHECK_THAT(drawing.tolerance(), Catch::Matchers::WithinRel(1e-9, 1e-12));

    const EntityId id = drawing.nextId();
    drawing.apply({.removed = {}, .added = {line(id, Vec3(0, 0, 0), Vec3(3000, 4000, 0))}});
    const auto box = drawing.bounds();
    REQUIRE(box.has_value());
    CHECK(box.value_or(fem::geom::Box{}).max == Vec3(3000, 4000, 0));
    CHECK_THAT(drawing.tolerance(), Catch::Matchers::WithinRel(5e-6, 1e-12));
}
