// Measures the analysis job against the interface (NFR-011-01, 02). Not
// part of ctest: run it on a Release build.
//
// Usage: bench_analysis_job [bays] [storeys]
//   The space frame of bench_line_mesh (default 26 x 26 x 24, about 100 000
//   pieces) meshed, with beams on every line, the column bases fixed and the
//   combination of bench_setup (self-weight, a load on every beam and on
//   every column). Then, with a timer of 10 ms on the thread of the
//   interface:
//   - the linear analysis of the combination by AnalysisJob, from start()
//     (which copies the document and the drawing) to finished(): the
//     largest gap between two ticks of the timer must be at most 100 ms;
//   - the same analysis cancelled as soon as the core assembles: the time
//     from cancel() to finished() must be at most 1 s.

#include "analysis_job.hpp"

#include <fem/geom/drawing.hpp>
#include <fem/mesher/line_mesher.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/editing.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

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
void build(Building& b, int bays, int storeys)
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

// The document of bench_setup, with the bases fixed: the combination.
s::ItemId prepare(s::Document& document, const Building& b)
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

// What a run of the job gave, timed on the thread of the interface.
struct Run
{
    double seconds = 0.0;    // from start() to finished()
    double largestGap = 0.0; // between two ticks of the timer, in s
    bool cancelled = false;
    double cancelToEnd = -1.0; // from cancel() to finished(), in s
    std::vector<s::CaseOutcome> outcomes;
};

Run timed(const s::Document& document, const Building& b, const s::MeshContext& mesh,
          s::ItemId item, bool cancelInAssembly)
{
    Run run;
    fem::app::AnalysisJob job;
    QEventLoop loop;
    QElapsedTimer clock;
    qint64 lastTick = 0;
    qint64 cancelledAt = -1;
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        const qint64 now = clock.nsecsElapsed();
        run.largestGap = std::max(run.largestGap, 1e-9 * static_cast<double>(now - lastTick));
        lastTick = now;
        if (cancelInAssembly && cancelledAt < 0) {
            const auto p = job.progress();
            if (!p.building && p.core.stage == c::RunStage::Assembly) {
                cancelledAt = clock.nsecsElapsed();
                job.cancel();
            }
        }
    });
    QObject::connect(&job, &fem::app::AnalysisJob::caseFinished, &loop,
                     [&](const s::CaseOutcome& o) { run.outcomes.push_back(o); });
    QObject::connect(&job, &fem::app::AnalysisJob::finished, &loop, [&](bool cancelled) {
        const qint64 now = clock.nsecsElapsed();
        run.seconds = 1e-9 * static_cast<double>(now);
        run.cancelled = cancelled;
        if (cancelledAt >= 0) {
            run.cancelToEnd = 1e-9 * static_cast<double>(now - cancelledAt);
        }
        loop.quit();
    });
    tick.start(10);
    clock.start();
    // The copy of the document and the drawing is on this thread: it counts.
    if (!job.start(document, b.drawing, mesh, {item})) {
        throw std::runtime_error("the job did not start");
    }
    lastTick = 0; // the gap of start() is measured from 0 by the first tick
    loop.exec();
    return run;
}

int run(int argc, char** argv)
{
    const int bays = argc > 1 ? std::atoi(argv[1]) : 26;
    const int storeys = argc > 2 ? std::atoi(argv[2]) : 24;
    if (bays < 1 || storeys < 1) {
        std::fputs("usage: bench_analysis_job [bays] [storeys], each at least 1\n", stderr);
        return EXIT_FAILURE;
    }
    Building b;
    build(b, bays, storeys);
    auto meshed = fem::mesher::meshLines(b.drawing);
    if (!meshed) {
        std::fputs("meshing failed\n", stderr);
        return EXIT_FAILURE;
    }
    s::MeshContext mesh;
    mesh.mesh = std::make_shared<const c::Mesh>(std::move(meshed->mesh));
    mesh.map = std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map));
    mesh.generation = 1;
    mesh.drawingRevision = b.drawing.revision();
    s::Document document;
    const s::ItemId uls = prepare(document, b);
    std::printf("%zu entities; %d nodes, %d elements\n", b.drawing.size(), mesh.mesh->nodeCount(),
                mesh.mesh->elementCount());

    // NFR-011-01: the interface while the job analyses.
    const Run full = timed(document, b, mesh, uls, false);
    if (full.outcomes.empty()) {
        std::fputs("the job gave no outcome\n", stderr);
        return EXIT_FAILURE;
    }
    if (const s::CaseOutcome& o = full.outcomes.front(); !o.result) {
        std::fprintf(stderr, "the analysis failed: %s (%zu issues)\n",
                     o.error ? o.error->message.c_str() : "refused", o.issues.size());
        for (std::size_t i = 0; i < std::min<std::size_t>(o.issues.size(), 5); ++i) {
            std::fprintf(stderr, "  %s\n", o.issues[i].detail.c_str());
        }
        return EXIT_FAILURE;
    }
    std::printf("analysis by the job: %.3f s; largest gap of the 10 ms timer: %.1f ms "
                "(NFR-011-01: at most 100 ms) -> %s\n",
                full.seconds, 1e3 * full.largestGap, full.largestGap <= 0.1 ? "ok" : "ABOVE");

    // NFR-011-02: cancelled in the assembly.
    const Run stopped = timed(document, b, mesh, uls, true);
    std::printf(
        "cancelled in the assembly: freed %.1f ms after cancel(), cancelled %s, largest "
        "gap %.1f ms (NFR-011-02: at most 1 s) -> %s\n",
        1e3 * stopped.cancelToEnd, stopped.cancelled ? "yes" : "NO", 1e3 * stopped.largestGap,
        stopped.cancelled && stopped.cancelToEnd >= 0.0 && stopped.cancelToEnd <= 1.0 ? "ok"
                                                                                      : "ABOVE");
    const bool met = full.largestGap <= 0.1 && stopped.cancelled && stopped.cancelToEnd >= 0.0 &&
                     stopped.cancelToEnd <= 1.0;
    return met ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[])
{
    const QCoreApplication application(argc, argv);
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
