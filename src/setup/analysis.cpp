#include "detail/check.hpp"

#include <fem/setup/analysis.hpp>

#include <algorithm>
#include <utility>

namespace fem::setup {

namespace {

// The analysis of the settings on the model built, as a CoreResult.
core::Expected<CoreResult> analyse(const core::Model& model, const AnalysisSettings& settings,
                                   const core::RunControl& control)
{
    const core::StaticOptions base{.threads = 0, .control = control};
    switch (settings.type) {
    case AnalysisType::SecondOrder:
        return core::runSecondOrderStatic(model, {.base = base,
                                                  .tolerance = settings.tolerance,
                                                  .maxIterations = settings.maxIterations})
            .map([](core::SecondOrderResult r) { return CoreResult{std::move(r)}; });
    case AnalysisType::Buckling:
        return core::runLinearBuckling(model, {.base = base, .count = settings.modes})
            .map([](core::BucklingResult r) { return CoreResult{std::move(r)}; });
    case AnalysisType::Linear:
        break;
    }
    return core::runLinearStatic(model, base).map([](core::StaticResult r) {
        return CoreResult{std::move(r)};
    });
}

} // namespace

CaseOutcome runCase(const Document& document, const geom::Drawing& drawing, const MeshContext& mesh,
                    ItemId item, const core::RunControl& control)
{
    CaseOutcome outcome;
    BuildResult built = buildModel(document, drawing, mesh, {.item = item});
    if (mesh.mesh) {
        detail::addCoreIssues(built);
    }
    outcome.issues = std::move(built.issues);
    // REQ-011-05: errors of the check refuse the case; warnings do not.
    if (std::ranges::any_of(outcome.issues,
                            [](const BuildIssue& i) { return i.severity == Severity::Error; })) {
        return outcome;
    }
    const AnalysisSettings& settings = document.analysis();
    auto data = analyse(built.model, settings, control);
    if (!data) {
        outcome.error = std::move(data.error()); // REQ-011-10
        return outcome;
    }
    outcome.result = AnalysisResult{.item = item,
                                    .settings = settings,
                                    .state = {.documentRevision = document.revision(),
                                              .drawingRevision = drawing.revision(),
                                              .meshGeneration = mesh.generation},
                                    .mesh = mesh.mesh,
                                    .data = std::move(*data),
                                    .warnings = outcome.issues};
    return outcome;
}

} // namespace fem::setup
