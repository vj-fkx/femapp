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

#include "analysis_building.hpp"
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

using fem::perf::build;
using fem::perf::Building;
using fem::perf::prepare;
namespace s = fem::setup;
namespace c = fem::core;

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
