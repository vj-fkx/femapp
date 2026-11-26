#include "analysis_text.hpp"
#include "frame_document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

// Feature 011, T13: REQ-011-07, 10, 13. The words of the analysis.

namespace {

namespace s = fem::setup;
namespace c = fem::core;
using fem::geom::Vec3;

const s::AnalysisResult& got(const std::optional<s::AnalysisResult>& value)
{
    REQUIRE(value.has_value());
    if (!value) {
        throw std::logic_error("no result");
    }
    return *value;
}

bool hasLine(const QStringList& lines, const QString& text)
{
    return lines.contains(text);
}

} // namespace

TEST_CASE("the progress of an analysis", "[app][analysis][text]")
{
    using fem::app::progressText;
    const QString uls = QStringLiteral("ULS");
    CHECK(progressText(uls, 1, 5, {.stage = c::RunStage::Assembly, .fraction = 0.4, .iteration = 3},
                       false) ==
          QStringLiteral("Analysing ULS (2 of 5): iteration 3, assembly 40%"));
    CHECK(progressText(uls, 0, 1,
                       {.stage = c::RunStage::Factorization, .fraction = -1.0, .iteration = 0},
                       false) == QStringLiteral("Analysing ULS: factorization"));
    CHECK(progressText(uls, 0, 1,
                       {.stage = c::RunStage::Iteration, .fraction = 0.1, .iteration = 7},
                       false) == QStringLiteral("Analysing ULS: iteration 7"));
    CHECK(progressText(uls, 2, 3, {}, true) ==
          QStringLiteral("Analysing ULS (3 of 3): building the model"));
    CHECK(progressText(uls, 0, 1, {.stage = c::RunStage::Recovery, .fraction = 1.0, .iteration = 0},
                       false) == QStringLiteral("Analysing ULS: bar forces 100%"));
    CHECK_FALSE(fem::app::analysisAlreadyRunning().isEmpty());
}

TEST_CASE("the names of the cases and of the analyses", "[app][analysis][text]")
{
    fem::setup::test::Frame frame;
    fem::setup::test::drawFrame(frame);
    fem::setup::test::Setup setup;
    fem::setup::test::prepare(setup, frame);
    CHECK(fem::app::caseName(setup.document, setup.loadCase) == QStringLiteral("lateral"));
    CHECK(fem::app::caseName(setup.document, s::ItemId{999}) == QStringLiteral("#999"));
    CHECK(fem::app::analysisTypeName(s::AnalysisType::Linear) == QStringLiteral("linear"));
    CHECK(fem::app::analysisTypeName(s::AnalysisType::SecondOrder) ==
          QStringLiteral("second order"));
    CHECK(fem::app::analysisTypeName(s::AnalysisType::Buckling) == QStringLiteral("buckling"));
}

TEST_CASE("the summary in the units of the document", "[app][analysis][text]")
{
    // A cantilever of 3 m along x with (10, -20) kN at its tip.
    fem::setup::test::Frame cantilever;
    cantilever.bars = {fem::setup::test::addLine(cantilever.drawing, Vec3(0, 0, 0), Vec3(3, 0, 0))};
    cantilever.bases = {fem::setup::test::addPoint(cantilever.drawing, Vec3(0, 0, 0))};
    cantilever.top = fem::setup::test::addPoint(cantilever.drawing, Vec3(3, 0, 0));
    fem::setup::test::Setup setup;
    fem::setup::test::prepare(setup, cantilever);
    s::Document& d = setup.document;
    const s::MeshContext mesh = fem::setup::test::meshOf(cantilever.drawing);
    const auto run = [&] {
        return got(s::runCase(d, cantilever.drawing, mesh, setup.loadCase, {}).result);
    };

    SECTION("linear, in kN and m")
    {
        const s::AnalysisResult result = run();
        const s::Summary summary = s::summarize(result);
        const QStringList lines = fem::app::summaryLines(QStringLiteral("lateral"), result,
                                                         s::UnitSystem::KilonewtonMetre);
        CHECK(lines.front() == QStringLiteral("lateral: linear analysis"));
        REQUIRE(summary.displacement.has_value());
        const s::Extreme& u = summary.displacement.value_or(s::Extreme{});
        CHECK(hasLine(lines, QStringLiteral("Largest displacement: %1 m at the node %2 in y")
                                 .arg(QString::number(u.value, 'g', 4))
                                 .arg(u.id)));
        CHECK(hasLine(lines, QStringLiteral("Sum of the reactions: (-10, 20, 0) kN; of the "
                                            "loads: (10, -20, 0) kN")));
        CHECK(hasLine(lines, QStringLiteral("Largest N: 10 kN in the element %1")
                                 .arg(summary.barForces[0].value_or(s::Extreme{}).id)));
        // The moment at the base, in kN·m.
        const s::Extreme& mz = summary.barForces[5].value_or(s::Extreme{});
        CHECK(hasLine(lines, QStringLiteral("Largest Mz: %1 kN·m in the element %2")
                                 .arg(QString::number(mz.value, 'g', 4))
                                 .arg(mz.id)));
        CHECK(std::abs(std::abs(mz.value) - 60.0) < 1e-6);
        CHECK(lines.filter(QStringLiteral("Converged")).isEmpty());
    }
    SECTION("second order: its iterations")
    {
        d.apply(s::changeAnalysis(d, {.type = s::AnalysisType::SecondOrder}));
        const s::AnalysisResult result = run();
        const s::Summary summary = s::summarize(result);
        const QStringList lines = fem::app::summaryLines(QStringLiteral("lateral"), result,
                                                         s::UnitSystem::KilonewtonMetre);
        CHECK(lines.front() == QStringLiteral("lateral: second order analysis"));
        CHECK(hasLine(lines, QStringLiteral("Converged in %1 iterations (last change %2)")
                                 .arg(summary.iterations)
                                 .arg(QString::number(summary.change, 'g', 4))));
    }
    SECTION("buckling: the factors and the note")
    {
        s::AnalysisResult result = run();
        c::BucklingResult buckling;
        buckling.modes.push_back(
            {.factor = 12.5, .displacements = {}, .rotations = {}, .warping = {}, .beams = {}});
        buckling.note = "Only 1 factor was found.";
        result.data = std::move(buckling);
        result.settings.type = s::AnalysisType::Buckling;
        const QStringList lines = fem::app::summaryLines(QStringLiteral("lateral"), result,
                                                         s::UnitSystem::KilonewtonMetre);
        CHECK(lines == QStringList{QStringLiteral("lateral: buckling analysis"),
                                   QStringLiteral("Buckling factor 1: 12.5"),
                                   QStringLiteral("Only 1 factor was found.")});
    }
}

TEST_CASE("refusals, failures and cancellations", "[app][analysis][text]")
{
    const QString uls = QStringLiteral("ULS");
    CHECK(fem::app::refusedText(uls, 1) ==
          QStringLiteral("ULS was not analysed: the check found a problem."));
    CHECK(fem::app::refusedText(uls, 3) ==
          QStringLiteral("ULS was not analysed: the check found 3 problems."));
    // The message of the core as it is: it names the nodes and elements
    // that the check panel picks (REQ-011-10, 18).
    const c::Error mechanism{.message = "The structure is a mechanism: the node 12 in v can move "
                                        "without resistance, with the element 3 (a support may "
                                        "be missing).",
                             .cancelled = false};
    CHECK(fem::app::failureText(uls, mechanism) ==
          QStringLiteral("ULS: ") + QString::fromStdString(mechanism.message));
    CHECK(
        fem::app::failureText(uls, {.message = "The analysis was cancelled.", .cancelled = true}) ==
        QStringLiteral("The analysis of ULS was cancelled."));
    CHECK(fem::app::runAllText(3, {QStringLiteral("W")}, false) ==
          QStringLiteral("Run all: 3 analysed, not analysed: W."));
    CHECK(fem::app::runAllText(1, {}, true) == QStringLiteral("Run all: 1 analysed (cancelled)."));
}
