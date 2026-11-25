#include <fem/core/stability_analysis.hpp>
#include <fem/setup/document.hpp>
#include <fem/setup/editing.hpp>

#include <catch2/catch_test_macros.hpp>

// Feature 011, T07 (design §4, §5.3): REQ-011-01. The type and the options
// of the analysis, in the document.

namespace {

namespace s = fem::setup;

} // namespace

TEST_CASE("the options of the analysis start at the defaults of the core", "[setup][analysis]")
{
    const s::Document document;
    const s::AnalysisSettings settings = document.analysis();
    CHECK(settings.type == s::AnalysisType::Linear);
    const fem::core::SecondOrderOptions secondOrder;
    CHECK(settings.tolerance == secondOrder.tolerance);
    CHECK(settings.maxIterations == secondOrder.maxIterations);
    const fem::core::BucklingOptions buckling;
    CHECK(settings.modes == buckling.count);
    CHECK(settings == s::AnalysisSettings{});
}

TEST_CASE("a change of the analysis is undone and redone", "[setup][analysis]")
{
    s::Document document;
    const s::AnalysisSettings before = document.analysis();
    const s::AnalysisSettings after{
        .type = s::AnalysisType::Buckling, .tolerance = 1e-6, .maxIterations = 20, .modes = 4};
    const s::DocumentEdit edit = s::changeAnalysis(document, after);
    REQUIRE_FALSE(edit.empty());
    CHECK(edit.removed.empty());
    CHECK(edit.added.empty());
    CHECK_FALSE(edit.units);
    CHECK_FALSE(edit.activeCase);

    const auto revision = document.revision();
    document.apply(edit);
    CHECK(document.analysis() == after);
    CHECK(document.revision() != revision);
    document.revert(edit);
    CHECK(document.analysis() == before);
    CHECK(document.revision() == revision);
    document.apply(edit);
    CHECK(document.analysis() == after);
}

TEST_CASE("the same options are an empty edit", "[setup][analysis]")
{
    s::Document document;
    CHECK(s::changeAnalysis(document, document.analysis()).empty());
    const s::AnalysisSettings second{.type = s::AnalysisType::SecondOrder};
    document.apply(s::changeAnalysis(document, second));
    CHECK(s::changeAnalysis(document, second).empty());
    // One option alone is a change.
    s::AnalysisSettings tighter = second;
    tighter.tolerance = 1e-10;
    CHECK_FALSE(s::changeAnalysis(document, tighter).empty());
}

TEST_CASE("a change of units leaves the analysis alone", "[setup][analysis]")
{
    s::Document document;
    const s::AnalysisSettings settings{
        .type = s::AnalysisType::SecondOrder, .tolerance = 1e-6, .maxIterations = 80, .modes = 3};
    document.apply(s::changeAnalysis(document, settings));
    const s::DocumentEdit units = s::changeUnits(document, s::UnitSystem::NewtonMillimetre);
    CHECK_FALSE(units.analysis);
    document.apply(units);
    CHECK(document.analysis() == settings);
    document.revert(units);
    CHECK(document.analysis() == settings);
}
