// Measures the preparation of a result for the viewport against the
// interface (NFR-012-02). Not part of ctest: run it on a Release build.
//
// Usage: bench_result_view [bays] [storeys]
//   The space frame of bench_line_mesh (default 26 x 26 x 24, about 100 000
//   pieces), meshed, with the document of bench_analysis_job, in a model
//   controller; the linear analysis of its combination (not timed). Then,
//   with a timer of 10 ms on the thread of the interface:
//   - the result shown by ResultView, from setShown(true) to ready(): the
//     samples, the scene and the values made on a worker thread; at most
//     2 s, and the largest gap between two ticks at most 100 ms;
//   - another quantity (Mz), the values only: the same limits.
//   The viewport is a sink that keeps what it gets.

#include "analysis_building.hpp"
#include "model_controller.hpp"
#include "result_view.hpp"

#include <fem/mesher/line_mesher.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/setup/analysis.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace s = fem::setup;
namespace c = fem::core;

struct Sink : fem::app::ResultSink
{
    std::shared_ptr<const fem::render::ResultScene> scene;
    void setResultScene(std::shared_ptr<const fem::render::ResultScene> s,
                        std::shared_ptr<const std::vector<float>> /*values*/) override
    {
        scene = std::move(s);
    }
    void setResultValues(std::shared_ptr<const std::vector<float>> /*values*/) override {}
    void setResultDisplay(const fem::render::ResultDisplay& /*display*/) override {}
    void setResultAnnotations(fem::render::ResultAnnotations /*annotations*/) override {}
    void setResultsShown(bool /*shown*/) override {}
};

struct Timed
{
    double seconds = 0.0;
    double largestGap = 0.0;
};

// From `act` (on this thread) to the view's ready(), timed.
Timed timed(fem::app::ResultView& view, const std::function<void()>& act)
{
    Timed out;
    QEventLoop loop;
    QElapsedTimer clock;
    qint64 lastTick = 0;
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        const qint64 now = clock.nsecsElapsed();
        out.largestGap = std::max(out.largestGap, 1e-9 * static_cast<double>(now - lastTick));
        lastTick = now;
    });
    QObject::connect(&view, &fem::app::ResultView::ready, &loop, [&] {
        out.seconds = 1e-9 * static_cast<double>(clock.nsecsElapsed());
        loop.quit();
    });
    tick.start(10);
    clock.start();
    act(); // the copy of the result is on this thread: it counts
    lastTick = 0;
    loop.exec();
    // The gap from the last tick to the end.
    out.largestGap = std::max(out.largestGap,
                              1e-9 * static_cast<double>(clock.nsecsElapsed() - lastTick) - 0.01);
    return out;
}

int run(int argc, char** argv)
{
    const int bays = argc > 1 ? std::atoi(argv[1]) : 26;
    const int storeys = argc > 2 ? std::atoi(argv[2]) : 24;
    if (bays < 1 || storeys < 1) {
        std::fputs("usage: bench_result_view [bays] [storeys], each at least 1\n", stderr);
        return EXIT_FAILURE;
    }
    fem::perf::Building b;
    fem::perf::build(b, bays, storeys);
    QUndoStack undo;
    fem::app::ModelController controller(&undo, b.drawing);
    s::Document document;
    const s::ItemId uls = fem::perf::prepare(document, b);
    s::DocumentEdit edit;
    for (const s::Item& item : document.items()) {
        edit.added.push_back(item);
    }
    controller.push(std::move(edit), QStringLiteral("frame"));
    auto meshed = fem::mesher::meshLines(b.drawing);
    if (!meshed) {
        std::fputs("meshing failed\n", stderr);
        return EXIT_FAILURE;
    }
    controller.meshGenerated(
        fem::render::buildMeshScene(std::make_shared<const c::Mesh>(std::move(meshed->mesh))),
        std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map)),
        b.drawing.revision());
    std::printf("%zu entities; %d nodes, %d elements\n", b.drawing.size(),
                controller.mesh().mesh->nodeCount(), controller.mesh().mesh->elementCount());
    std::fflush(stdout);
    const s::CaseOutcome outcome =
        s::runCase(controller.document(), b.drawing, controller.mesh(), uls, {});
    if (!outcome.result) {
        std::fputs("the analysis failed\n", stderr);
        return EXIT_FAILURE;
    }
    controller.caseFinished(outcome);

    // What the thread of the interface does, piece by piece.
    {
        QElapsedTimer clock;
        clock.start();
        const std::shared_ptr<const s::AnalysisResult> copy = controller.results().share(uls);
        const double copied = 1e-6 * static_cast<double>(clock.nsecsElapsed());
        clock.restart();
        controller.rebuildSymbols();
        const double symbols = 1e-6 * static_cast<double>(clock.nsecsElapsed());
        clock.restart();
        fem::app::SymbolOptions shown = controller.symbolOptions();
        shown.loadsShown = false;
        controller.setSymbolOptions(shown);
        shown.loadsShown = true;
        controller.setSymbolOptions(shown);
        const double overlay = 1e-6 * static_cast<double>(clock.nsecsElapsed()) / 2;
        clock.restart();
        const auto prepared =
            fem::app::prepareResult(copy, 0, fem::core::ResultQuantity::UMagnitude);
        const double prepare = 1e-6 * static_cast<double>(clock.nsecsElapsed());
        clock.restart();
        const auto reactions = fem::app::reactionSymbols(*copy);
        const double reacted = 1e-6 * static_cast<double>(clock.nsecsElapsed());
        clock.restart();
        fem::app::AnnotationInput in;
        in.result = copy.get();
        in.fields = &prepared->fields;
        in.shape = prepared->shape.get();
        in.field = &prepared->field;
        in.display = fem::app::displayFor(in.settings, prepared->field, prepared->autoScale, false);
        const auto annotations = fem::app::annotationsFor(in);
        const double annotated = 1e-6 * static_cast<double>(clock.nsecsElapsed());
        clock.restart();
        std::printf("pieces: share %.1f ms, symbols rebuilt %.1f ms, over them %.1f ms, prepare "
                    "%.1f ms, reactions %.1f ms, "
                    "texts %.1f ms (%zu labels, %zu reactions)\n",
                    copied, symbols, overlay, prepare, reacted, annotated,
                    annotations.labels.size(), reactions.forces.size());
        std::fflush(stdout);
    }

    Sink sink;
    fem::app::ResultView view(&controller, &sink);
    view.showResult(uls, 0);
    const Timed whole = timed(view, [&] { view.setShown(true); });
    if (!view.prepared() || !sink.scene) {
        std::fputs("the result was not shown\n", stderr);
        return EXIT_FAILURE;
    }
    const auto& shape = *view.prepared()->shape;
    std::printf("samples %zu, segments %zu, %.1f MB on the GPU (%.1f bytes per sample)\n",
                shape.positions.size(), sink.scene->segmentStarts().size(),
                1e-6 * static_cast<double>(sink.scene->gpuBytes()),
                static_cast<double>(sink.scene->gpuBytes()) /
                    static_cast<double>(shape.positions.size()));
    std::printf("the result shown: %.3f s (at most 2 s); largest gap of the 10 ms timer: "
                "%.1f ms (at most 100 ms) -> %s\n",
                whole.seconds, 1e3 * whole.largestGap,
                whole.seconds <= 2.0 && whole.largestGap <= 0.1 ? "ok" : "ABOVE");

    fem::app::ResultDisplaySettings settings = view.settings();
    settings.quantity = fem::core::ResultQuantity::Mz;
    const Timed values = timed(view, [&] { view.setSettings(settings); });
    std::printf("another quantity: %.3f s; largest gap %.1f ms -> %s\n", values.seconds,
                1e3 * values.largestGap,
                values.seconds <= 2.0 && values.largestGap <= 0.1 ? "ok" : "ABOVE");
    const bool met = whole.seconds <= 2.0 && whole.largestGap <= 0.1 && values.seconds <= 2.0 &&
                     values.largestGap <= 0.1;
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
