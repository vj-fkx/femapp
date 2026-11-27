// Feature 012 (design §6): the display of a result, with the viewport
// replaced by a sink that records what it gets.

#include "frame_document.hpp"
#include "model_controller.hpp"
#include "result_view.hpp"
#include "test_application.hpp"

#include <fem/mesher/line_mesher.hpp>
#include <fem/render/mesh_scene.hpp>
#include <fem/setup/editing.hpp>

#include <QEventLoop>
#include <QTimer>
#include <QUndoStack>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::app::ResultView;
using fem::core::ResultQuantity;

struct Sink : fem::app::ResultSink
{
    int scenes = 0;
    int values = 0;
    bool shown = false;
    std::shared_ptr<const fem::render::ResultScene> scene;
    std::optional<fem::render::ResultDisplay> display;
    fem::render::ResultAnnotations annotations;

    void setResultScene(std::shared_ptr<const fem::render::ResultScene> s,
                        std::shared_ptr<const std::vector<float>> /*values*/) override
    {
        ++scenes;
        scene = std::move(s);
    }
    void setResultValues(std::shared_ptr<const std::vector<float>> /*values*/) override
    {
        ++values;
    }
    void setResultDisplay(const fem::render::ResultDisplay& d) override { display = d; }
    void setResultAnnotations(fem::render::ResultAnnotations a) override
    {
        annotations = std::move(a);
    }
    void setResultsShown(bool on) override { shown = on; }
};

const fem::render::ResultDisplay& displayOf(const Sink& sink)
{
    if (!sink.display) {
        throw std::logic_error("no display");
    }
    return *sink.display;
}

// The frame of 010 in the controller, with a second load case, both
// analysed.
struct Analysed
{
    fem::setup::test::Frame frame;
    fem::setup::test::Setup setup;
    s::ItemId vertical{};
};

void fill(Analysed& a, fem::app::ModelController& controller)
{
    fem::setup::test::drawFrame(a.frame);
    fem::setup::test::prepare(a.setup, a.frame);
    s::Document& d = a.setup.document;
    a.vertical = fem::setup::test::added(
        d,
        s::addItem(d, s::LoadCase{.id = {}, .name = "vertical", .standard = {}, .category = {}}));
    fem::setup::test::added(
        d, s::addItem(d, s::LoadItem{.id = {},
                                     .loadCase = a.vertical,
                                     .targets = fem::setup::test::entities({a.frame.top}),
                                     .action = c::NodalForce{
                                         .nodes = {}, .force = fem::geom::Vec3(0, -50, 0)}}));
    s::DocumentEdit edit;
    for (const s::Item& item : d.items()) {
        edit.added.push_back(item);
    }
    controller.push(std::move(edit), QStringLiteral("frame"));
    // The mesh of the controller, so that the results are current.
    auto meshed = fem::mesher::meshLines(a.frame.drawing);
    REQUIRE(meshed.has_value());
    controller.meshGenerated(
        fem::render::buildMeshScene(std::make_shared<const c::Mesh>(std::move(meshed->mesh))),
        std::make_shared<const fem::mesher::LineMeshMap>(std::move(meshed->map)),
        a.frame.drawing.revision());
    for (const s::ItemId item : {a.setup.loadCase, a.vertical}) {
        const s::CaseOutcome outcome =
            s::runCase(controller.document(), a.frame.drawing, controller.mesh(), item, {});
        REQUIRE(outcome.result.has_value());
        controller.caseFinished(outcome);
    }
    REQUIRE(controller.results().find(a.vertical) != nullptr);
    REQUIRE(s::ResultSet::isCurrent(*controller.results().find(a.vertical), controller.state()));
}

// Runs the event loop until the view says it is ready.
void waitReady(ResultView& view)
{
    QEventLoop loop;
    QObject::connect(&view, &ResultView::ready, &loop, &QEventLoop::quit);
    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    bool done = false;
    QObject::connect(&view, &ResultView::ready, [&done] { done = true; });
    loop.exec();
    REQUIRE(done);
}

} // namespace

// REQ-012-01, 02, 04, 08, 13, AC-7.
TEST_CASE("the display of a result follows the dock and the settings", "[app][result][view]")
{
    fem::app::test::application();
    Analysed a;
    QUndoStack undo;
    fem::app::ModelController controller(&undo, a.frame.drawing);
    fill(a, controller);
    Sink sink;
    ResultView view(&controller, &sink);

    // Nothing chosen: no display.
    view.setShown(true);
    CHECK_FALSE(view.isShown());
    CHECK_FALSE(sink.shown);

    view.showResult(a.setup.loadCase, 0);
    CHECK(view.isAvailable());
    view.setShown(true);
    CHECK(sink.shown);
    waitReady(view);
    CHECK(view.sceneBuilds() == 1);
    CHECK(sink.scenes == 1);
    REQUIRE(sink.scene);
    CHECK_FALSE(sink.scene->segmentStarts().empty());
    // REQ-012-03: the loads go, the reactions come.
    CHECK_FALSE(controller.symbolOptions().loadsShown);
    CHECK_FALSE(controller.symbolOptions().reactionForces.empty());

    // Another quantity and the user's factor: the values only.
    fem::app::ResultDisplaySettings settings = view.settings();
    settings.quantity = ResultQuantity::Mz;
    settings.scaleMode = fem::app::ScaleMode::User;
    settings.userScale = 40.0;
    view.setSettings(settings);
    waitReady(view);
    CHECK(view.sceneBuilds() == 1);
    CHECK(view.valueBuilds() == 1);
    CHECK(sink.scenes == 1);
    CHECK(sink.values == 1);
    CHECK(displayOf(sink).scale == 40.0);

    // REQ-012-08: the factor, the bands, the original: no job.
    settings.userScale = 80.0;
    settings.bands = 5;
    settings.original = true;
    view.setSettings(settings);
    CHECK(view.sceneBuilds() == 1);
    CHECK(view.valueBuilds() == 1);
    CHECK(displayOf(sink).scale == 80.0);
    CHECK(displayOf(sink).bands == 5);
    CHECK(displayOf(sink).original);

    // REQ-012-04: another case keeps the quantity and the factor.
    view.showResult(a.vertical, 0);
    waitReady(view);
    CHECK(view.sceneBuilds() == 2);
    CHECK(sink.scenes == 2);
    CHECK(displayOf(sink).scale == 80.0);
    REQUIRE(view.prepared());
    CHECK(view.prepared()->field.quantity == ResultQuantity::Mz);
    REQUIRE(sink.annotations.legend.has_value());
    CHECK(sink.annotations.headline.front().startsWith(QStringLiteral("vertical")));
    CHECK_FALSE(sink.annotations.warning);

    // REQ-012-02: an edit of the model: the warning, on the same scene.
    const auto drawn = sink.scene;
    controller.push(s::setActiveCase(controller.document(), a.setup.loadCase),
                    QStringLiteral("active"));
    CHECK(sink.annotations.warning);
    CHECK(sink.scene == drawn);
    CHECK(view.sceneBuilds() == 2);
    undo.undo();
    CHECK_FALSE(sink.annotations.warning);

    // Hidden: the model symbols again.
    view.setShown(false);
    CHECK_FALSE(sink.shown);
    CHECK(controller.symbolOptions().loadsShown);
    CHECK(controller.symbolOptions().reactionForces.empty());

    // The item removed: no result to show.
    view.setShown(true);
    CHECK(sink.shown);
    controller.push(s::removeItem(controller.document(), a.vertical), QStringLiteral("remove"));
    CHECK_FALSE(view.isAvailable());
    CHECK_FALSE(view.isShown());
    CHECK_FALSE(sink.shown);
}

// Design §6: a request made while a job runs waits for it; the job of an
// older request is dropped.
TEST_CASE("an older request of the result display is dropped", "[app][result][view]")
{
    fem::app::test::application();
    Analysed a;
    QUndoStack undo;
    fem::app::ModelController controller(&undo, a.frame.drawing);
    fill(a, controller);
    Sink sink;
    ResultView view(&controller, &sink);
    view.showResult(a.setup.loadCase, 0);
    view.setShown(true);            // a job starts for the first case
    view.showResult(a.vertical, 0); // waits for it
    int readies = 0;
    QObject::connect(&view, &ResultView::ready, [&readies] { ++readies; });
    waitReady(view);
    CHECK(readies == 1);
    CHECK(view.sceneBuilds() == 2);
    // Only the last one reached the viewport.
    CHECK(sink.scenes == 1);
    REQUIRE(view.prepared());
    CHECK(view.prepared()->result->item == a.vertical);
}
