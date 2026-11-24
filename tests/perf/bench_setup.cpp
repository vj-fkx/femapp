// Measures the model document (NFR-010-01). Not part of ctest: run it on a
// Release build.
//
// Usage: bench_setup [bays] [storeys]
//   The space frame of bench_line_mesh (default 26 x 26 x 24, about 100 000
//   pieces) meshed, then: one bar assignment on every entity (and its undo
//   and redo), and the build of the core::Model of a combination of three
//   load cases (self-weight, a load on every beam, a load on every column).

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using fem::geom::EntityId;
using fem::geom::Vec3;
namespace s = fem::setup;
namespace c = fem::core;

double secondsSince(Clock::time_point start)
{
    return std::chrono::duration<double>(Clock::now() - start).count();
}

struct Building
{
    fem::geom::Drawing drawing;
    std::vector<EntityId> all;
    std::vector<EntityId> columns;
    std::vector<EntityId> beams;
};

// The frame of bench_line_mesh.
void build(Building& b, int bays, int storeys)
{
    const double width = 6.0;
    const double height = 3.5;
    fem::geom::DrawingEdit edit;
    const auto line = [&](const Vec3& p, const Vec3& q, std::vector<EntityId>* kind) {
        const EntityId id = b.drawing.nextId();
        edit.added.push_back(
            fem::geom::Entity{.id = id, .shape = fem::geom::LineEntity{.start = p, .end = q}});
        b.all.push_back(id);
        if (kind != nullptr) {
            kind->push_back(id);
        }
    };
    const auto at = [&](int i, int j, int k) { return Vec3(width * i, width * j, height * k); };
    for (int i = 0; i <= bays; ++i) {
        for (int j = 0; j <= bays; ++j) {
            line(at(i, j, 0), at(i, j, storeys), &b.columns);
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

s::ItemId apply(s::Document& document, const s::EditResult& result)
{
    if (!result) {
        throw std::runtime_error("an edit was refused");
    }
    document.apply(*result);
    return s::idOf(result->added.back());
}

s::Targets targetsOf(const std::vector<EntityId>& ids)
{
    s::Targets t;
    t.entities = ids;
    return t;
}

int run(int argc, char** argv)
{
    const int bays = argc > 1 ? std::atoi(argv[1]) : 26;
    const int storeys = argc > 2 ? std::atoi(argv[2]) : 24;
    Building b;
    build(b, bays, storeys);
    auto start = Clock::now();
    auto meshed = fem::mesher::meshLines(b.drawing);
    if (!meshed) {
        std::fputs("meshing failed\n", stderr);
        return 1;
    }
    s::MeshContext mesh;
    mesh.mesh = std::make_shared<const c::Mesh>(std::move(meshed->mesh));
    mesh.map = std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map));
    mesh.generation = 1;
    mesh.drawingRevision = b.drawing.revision();
    std::printf("%zu entities (%zu columns, %zu beams); meshed in %.3f s: %d nodes, %d elements\n",
                b.all.size(), b.columns.size(), b.beams.size(), secondsSince(start),
                mesh.mesh->nodeCount(), mesh.mesh->elementCount());

    s::Document document;
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

    // NFR-010-01: one assignment on every entity, and its undo and redo.
    start = Clock::now();
    const auto edit = s::assign(document, s::BarAssignment{.id = {},
                                                           .targets = targetsOf(b.all),
                                                           .data = {.elements = {},
                                                                    .kind = c::ElementKind::Beam3D,
                                                                    .material = "steel",
                                                                    .section = "I"}});
    if (!edit) {
        std::fputs("the assignment was refused\n", stderr);
        return 1;
    }
    document.apply(*edit);
    const double assigned = secondsSince(start);
    // A second assignment that takes half of them from the first.
    const std::vector<EntityId> half(b.all.begin(),
                                     b.all.begin() + static_cast<long>(b.all.size() / 2));
    start = Clock::now();
    const auto again = s::assign(document, s::BarAssignment{.id = {},
                                                            .targets = targetsOf(half),
                                                            .data = {.elements = {},
                                                                     .kind = c::ElementKind::Beam3D,
                                                                     .material = "steel",
                                                                     .section = "I"}});
    if (!again) {
        std::fputs("the second assignment was refused\n", stderr);
        return 1;
    }
    document.apply(*again);
    const double reassigned = secondsSince(start);
    start = Clock::now();
    document.revert(*again);
    const double undone = secondsSince(start);
    start = Clock::now();
    document.apply(*again);
    const double redone = secondsSince(start);
    std::printf("assign to all: %.3f s; take half of them: %.3f s; undo %.3f s; redo %.3f s\n",
                assigned, reassigned, undone, redone);

    // NFR-010-01: the build of a combination of three cases.
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
    const s::ItemId combination = apply(
        document, s::addItem(document, s::Combination{.id = {},
                                                      .name = "ULS",
                                                      .terms = {{g, 1.35}, {q, 1.5}, {w, 0.9}}}));
    double best = 1e300;
    std::size_t issues = 0;
    for (int k = 0; k < 3; ++k) {
        start = Clock::now();
        const s::BuildResult built =
            s::buildModel(document, b.drawing, mesh, {.item = combination});
        best = std::min(best, secondsSince(start));
        issues = built.issues.size();
    }
    std::printf("build of the combination: %.3f s (best of 3), %zu issues\n", best, issues);
    const bool met = assigned <= 1.0 && undone <= 1.0 && best <= 2.0;
    std::printf("targets (NFR-010-01): assign <= 1 s, undo <= 1 s, build <= 2 s: %s\n",
                met ? "met" : "NOT met");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
}
