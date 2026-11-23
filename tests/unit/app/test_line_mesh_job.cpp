#include "line_mesh_job.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// Feature 009, T11 (REQ-009-13, 14): the mesh of a drawing made on a worker
// thread and handed back on the thread of the job.

namespace {

using fem::geom::Drawing;
using fem::geom::DrawingEdit;
using fem::geom::Entity;
using fem::geom::LineEntity;
using fem::geom::Vec3;

// One application for the whole executable.
QCoreApplication& application()
{
    static int argc = 1;
    static std::string name = "test_app";
    static std::array<char*, 2> argv{name.data(), nullptr};
    static QCoreApplication app(argc, argv.data());
    return app;
}

void addLine(Drawing& drawing, const Vec3& a, const Vec3& b)
{
    DrawingEdit edit;
    edit.added.push_back(Entity{.id = drawing.nextId(), .shape = LineEntity{.start = a, .end = b}});
    drawing.apply(edit);
}

struct Outcome
{
    std::shared_ptr<const fem::render::MeshScene> scene;
    bool finished = false;
    fem::mesher::LineMeshReport report;
    std::uint64_t revision = 0;
    bool failed = false;
    fem::mesher::LineMeshError error;
};

// Waits for the job to finish or fail (at most 30 s).
void waitFor(fem::app::LineMeshJob& job, Outcome& outcome)
{
    QEventLoop loop;
    QObject::connect(&job, &fem::app::LineMeshJob::finished, &loop,
                     [&](std::shared_ptr<const fem::render::MeshScene> scene,
                         const fem::mesher::LineMeshReport& report, std::uint64_t revision) {
                         outcome.scene = std::move(scene);
                         outcome.finished = true;
                         outcome.report = report;
                         outcome.revision = revision;
                         loop.quit();
                     });
    QObject::connect(&job, &fem::app::LineMeshJob::failed, &loop,
                     [&](const fem::mesher::LineMeshError& error) {
                         outcome.failed = true;
                         outcome.error = error;
                         loop.quit();
                     });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

TEST_CASE("a drawing is meshed on a worker thread", "[app][mesh]")
{
    application();
    Drawing drawing;
    addLine(drawing, Vec3(0, 0, 0), Vec3(0, 6, 0));
    addLine(drawing, Vec3(0, 3, 0), Vec3(4, 3, 0));
    fem::app::LineMeshJob job;
    REQUIRE(job.start(drawing, {}));
    CHECK(job.isRunning());
    // A second start while the first runs is ignored.
    CHECK_FALSE(job.start(drawing, {}));
    Outcome outcome;
    waitFor(job, outcome);
    CHECK_FALSE(job.isRunning());
    REQUIRE(outcome.scene != nullptr);
    CHECK(outcome.scene->mesh().nodeCount() == 4);
    REQUIRE(outcome.finished);
    CHECK(outcome.report.elements == 3);
    CHECK(outcome.report.splits.size() == 1);
    CHECK(outcome.revision == drawing.revision());
    // Free again.
    CHECK(job.start(drawing, {}));
    Outcome again;
    waitFor(job, again);
    CHECK(again.scene != nullptr);
}

TEST_CASE("a drawing without lines fails", "[app][mesh]")
{
    application();
    const Drawing drawing;
    fem::app::LineMeshJob job;
    REQUIRE(job.start(drawing, {}));
    Outcome outcome;
    waitFor(job, outcome);
    CHECK(outcome.scene == nullptr);
    REQUIRE(outcome.failed);
    CHECK(outcome.error.kind == fem::mesher::LineMeshError::Kind::NoLines);
}
