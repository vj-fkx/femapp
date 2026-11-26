#include "analysis_job.hpp"
#include "frame_document.hpp"
#include "test_application.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

// Feature 011, T11 (design §5.4): REQ-011-03, 04, 06, 07, 08, NFR-011-02.
// The analysis of several cases on a worker thread.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::app::AnalysisJob;
using fem::app::test::application;
using fem::geom::Vec3;
using fem::setup::test::added;
using fem::setup::test::drawFrame;
using fem::setup::test::entities;
using fem::setup::test::Frame;
using fem::setup::test::meshOf;
using fem::setup::test::prepare;
using fem::setup::test::Setup;

// The value of an optional the test requires.
template <typename T>
const T& got(const std::optional<T>& value)
{
    REQUIRE(value.has_value());
    if (!value) {
        throw std::logic_error("no value");
    }
    return *value;
}

// What the job reported.
struct Report
{
    std::vector<s::CaseOutcome> outcomes;
    bool finished = false;
    bool cancelled = false;
};

void listen(AnalysisJob& job, Report& report, QEventLoop& loop)
{
    QObject::connect(&job, &AnalysisJob::caseFinished, &loop,
                     [&](const s::CaseOutcome& o) { report.outcomes.push_back(o); });
    QObject::connect(&job, &AnalysisJob::finished, &loop, [&](bool cancelled) {
        report.finished = true;
        report.cancelled = cancelled;
        loop.quit();
    });
}

// The frame of 010 with a second case, a combination and a case whose load
// is on an entity that is not in the drawing.
struct Model
{
    Frame frame;
    Setup setup;
    s::MeshContext mesh;
    s::ItemId second{};
    s::ItemId combination{};
    s::ItemId invalid{};
};

void build(Model& m)
{
    drawFrame(m.frame);
    prepare(m.setup, m.frame);
    m.mesh = meshOf(m.frame.drawing);
    s::Document& d = m.setup.document;
    m.second =
        added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "G", .standard = {}, .category = {}}));
    added(d, s::addItem(
                 d, s::LoadItem{.id = {},
                                .loadCase = m.second,
                                .targets = entities({m.frame.top}),
                                .action = c::NodalForce{.nodes = {}, .force = Vec3(0, -50, 0)}}));
    m.combination = added(
        d, s::addItem(d, s::Combination{.id = {},
                                        .name = "ULS",
                                        .terms = {{m.second, 1.35}, {m.setup.loadCase, 1.5}}}));
    m.invalid =
        added(d, s::addItem(d, s::LoadCase{.id = {}, .name = "W", .standard = {}, .category = {}}));
    added(d,
          s::addItem(d, s::LoadItem{.id = {},
                                    .loadCase = m.invalid,
                                    .targets = entities({fem::geom::EntityId{999}}),
                                    .action = c::NodalForce{.nodes = {}, .force = Vec3(1, 0, 0)}}));
}

} // namespace

TEST_CASE("run all analyses the items in order, past an invalid one", "[app][analysis]")
{
    application();
    Model m;
    build(m);
    AnalysisJob job;
    QEventLoop loop;
    Report report;
    listen(job, report, loop);
    const std::vector<s::ItemId> items{m.setup.loadCase, m.second, m.invalid, m.combination};
    REQUIRE(job.start(m.setup.document, m.frame.drawing, m.mesh, items));
    CHECK(job.isRunning());
    // REQ-011-07: a second request is refused while it runs.
    CHECK_FALSE(job.start(m.setup.document, m.frame.drawing, m.mesh, items));
    // REQ-011-06: an edit made meanwhile does not reach the run.
    m.setup.document.apply(
        s::changeAnalysis(m.setup.document, {.type = s::AnalysisType::Buckling}));
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();

    REQUIRE(report.finished);
    CHECK_FALSE(report.cancelled);
    CHECK_FALSE(job.isRunning());
    REQUIRE(report.outcomes.size() == 4);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const s::CaseOutcome& o = report.outcomes[i];
        INFO("item " << i);
        if (items[i] == m.invalid) {
            CHECK_FALSE(o.result.has_value());
            CHECK_FALSE(o.error.has_value());
            CHECK_FALSE(o.issues.empty());
        } else {
            REQUIRE(o.result.has_value());
            CHECK(got(o.result).item == items[i]);
            CHECK(got(o.result).settings.type == s::AnalysisType::Linear);
        }
    }
    const AnalysisJob::Progress last = job.progress();
    CHECK(last.count == 4);
    CHECK(last.index == 3);
    CHECK(last.item == m.combination);

    // Done: it starts again.
    Report again;
    QEventLoop second;
    listen(job, again, second);
    REQUIRE(job.start(m.setup.document, m.frame.drawing, m.mesh, {m.second}));
    QTimer::singleShot(60000, &second, &QEventLoop::quit);
    second.exec();
    REQUIRE(again.outcomes.size() == 1);
    CHECK(got(again.outcomes.front().result).settings.type == s::AnalysisType::Buckling);
}

TEST_CASE("a cancelled run stops within a second, without the items after", "[app][analysis]")
{
    application();
    Model m;
    build(m);
    // A second order that does not converge (a negative tolerance) and runs
    // for very many iterations, until it is cancelled.
    m.setup.document.apply(s::changeAnalysis(
        m.setup.document,
        {.type = s::AnalysisType::SecondOrder, .tolerance = -1.0, .maxIterations = 100000000}));
    AnalysisJob job;
    QEventLoop loop;
    Report report;
    listen(job, report, loop);
    REQUIRE(job.start(m.setup.document, m.frame.drawing, m.mesh, {m.setup.loadCase, m.second}));

    // REQ-011-07: the progress shows the index, the case and the stage.
    QElapsedTimer clock;
    qint64 cancelledAt = -1;
    AnalysisJob::Progress seen;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        const AnalysisJob::Progress p = job.progress();
        if (cancelledAt < 0 && !p.building && p.core.stage == c::RunStage::Iteration &&
            p.core.iteration >= 3) {
            seen = p;
            cancelledAt = clock.elapsed();
            job.cancel();
        }
    });
    clock.start();
    poll.start(5);
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();
    const qint64 endedAt = clock.elapsed();

    REQUIRE(cancelledAt >= 0);
    CHECK(seen.index == 0);
    CHECK(seen.count == 2);
    CHECK(seen.item == m.setup.loadCase);
    CHECK(seen.core.fraction >= 0.0);
    REQUIRE(report.finished);
    CHECK(report.cancelled);
    CHECK(endedAt - cancelledAt <= 1000); // NFR-011-02
    // The case running comes back cancelled, and the next one does not run.
    REQUIRE(report.outcomes.size() == 1);
    CHECK_FALSE(report.outcomes.front().result.has_value());
    REQUIRE(report.outcomes.front().error.has_value());
    CHECK(got(report.outcomes.front().error).cancelled);
}
