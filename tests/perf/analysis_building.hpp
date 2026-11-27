#pragma once

// The space frame of bench_line_mesh with the document of bench_setup (the
// bases fixed, self-weight, a load on every beam and every column, and
// their combination), for the benchmarks of the analysis job (011) and of
// the result display (012).

#include <fem/geom/drawing.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/editing.hpp>

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fem::perf {

using fem::geom::EntityId;
using fem::geom::Vec3;
namespace s = fem::setup;
namespace c = fem::core;

struct Building
{
    fem::geom::Drawing drawing;
    std::vector<EntityId> all;
    std::vector<EntityId> columns;
    std::vector<EntityId> beams;
    std::vector<EntityId> bases;
};

// The frame of bench_line_mesh, with a point at the base of every column.
inline void build(Building& b, int bays, int storeys)
{
    const double width = 6.0;
    const double height = 3.5;
    fem::geom::DrawingEdit edit;
    const auto add = [&](fem::geom::EntityShape shape) {
        const EntityId id = b.drawing.nextId();
        edit.added.push_back(fem::geom::Entity{.id = id, .shape = std::move(shape)});
        return id;
    };
    const auto line = [&](const Vec3& p, const Vec3& q, std::vector<EntityId>* kind) {
        const EntityId id = add(fem::geom::LineEntity{.start = p, .end = q});
        b.all.push_back(id);
        if (kind != nullptr) {
            kind->push_back(id);
        }
    };
    const auto at = [&](int i, int j, int k) { return Vec3(width * i, width * j, height * k); };
    for (int i = 0; i <= bays; ++i) {
        for (int j = 0; j <= bays; ++j) {
            line(at(i, j, 0), at(i, j, storeys), &b.columns);
            b.bases.push_back(add(fem::geom::PointEntity{.position = at(i, j, 0)}));
        }
    }
    for (int k = 1; k <= storeys; ++k) {
        for (int a = 0; a < bays; ++a) {
            for (int n = 0; n <= bays; ++n) {
                line(at(a, n, k), at(a + 1, n, k), &b.beams);
                line(at(n, a, k), at(n, a + 1, k), &b.beams);
            }
        }
    }
    for (int k = 0; k < storeys; ++k) {
        for (int a = 0; a < bays; ++a) {
            for (int n = 0; n <= bays; ++n) {
                line(at(a, n, k), at(a + 1, n, k + 1), nullptr);
                line(at(a + 1, n, k), at(a, n, k + 1), nullptr);
                line(at(n, a, k), at(n, a + 1, k + 1), nullptr);
                line(at(n, a + 1, k), at(n, a, k + 1), nullptr);
            }
        }
    }
    b.drawing.apply(edit);
}

inline s::ItemId apply(s::Document& document, const s::EditResult& result)
{
    if (!result) {
        throw std::runtime_error("an edit was refused");
    }
    document.apply(*result);
    return s::idOf(result->added.back());
}

inline s::Targets targetsOf(const std::vector<EntityId>& ids)
{
    s::Targets t;
    t.entities = ids;
    return t;
}

// The document of bench_setup, with the bases fixed: the combination.
inline s::ItemId prepare(s::Document& document, const Building& b)
{
    apply(document, s::addItem(document, s::MaterialItem{.id = {},
                                                         .data = {.name = "steel",
                                                                  .youngsModulus = 2.1e8,
                                                                  .poissonsRatio = 0.3,
                                                                  .density = 7.85},
                                                         .standard = {},
                                                         .grade = {},
                                                         .properties = {}}));
    apply(document,
          s::addItem(
              document,
              s::SectionItem{.id = {},
                             .data = {.name = "I",
                                      .shape = c::ISection{
                                          .h = 0.3, .bf = 0.15, .tf = 0.0107, .tw = 0.0071}}}));
    apply(document, s::assign(document, s::BarAssignment{.id = {},
                                                         .targets = targetsOf(b.all),
                                                         .data = {.elements = {},
                                                                  .kind = c::ElementKind::Beam3D,
                                                                  .material = "steel",
                                                                  .section = "I"}}));
    s::SupportItem fixed{.id = {}, .targets = targetsOf(b.bases), .held = {}};
    for (const c::DofType d : {c::DofType::U, c::DofType::V, c::DofType::W, c::DofType::RX,
                               c::DofType::RY, c::DofType::RZ}) {
        fixed.held[static_cast<std::size_t>(d)] = true;
    }
    apply(document, s::assign(document, fixed));
    const s::ItemId g = apply(
        document,
        s::addItem(document, s::LoadCase{.id = {}, .name = "G", .standard = {}, .category = {}}));
    const s::ItemId q = apply(
        document,
        s::addItem(document, s::LoadCase{.id = {}, .name = "Q", .standard = {}, .category = {}}));
    const s::ItemId w = apply(
        document,
        s::addItem(document, s::LoadCase{.id = {}, .name = "W", .standard = {}, .category = {}}));
    apply(document,
          s::addItem(document,
                     s::LoadItem{.id = {},
                                 .loadCase = g,
                                 .targets = {},
                                 .action = s::SelfWeight{.acceleration = Vec3(0, 0, -9.81)}}));
    apply(document,
          s::addItem(document,
                     s::LoadItem{.id = {},
                                 .loadCase = q,
                                 .targets = targetsOf(b.beams),
                                 .action = c::DistributedLoad{.elements = {},
                                                              .startValue = Vec3(0, 0, -5),
                                                              .endValue = Vec3(0, 0, -5)}}));
    apply(
        document,
        s::addItem(document, s::LoadItem{.id = {},
                                         .loadCase = w,
                                         .targets = targetsOf(b.columns),
                                         .action = c::DistributedLoad{.elements = {},
                                                                      .startValue = Vec3(1, 0, 0),
                                                                      .endValue = Vec3(1, 0, 0)}}));
    return apply(document,
                 s::addItem(document, s::Combination{.id = {},
                                                     .name = "ULS",
                                                     .terms = {{g, 1.35}, {q, 1.5}, {w, 0.9}}}));
}

} // namespace fem::perf
