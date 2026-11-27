#include "detail/check.hpp"

#include <fem/setup/analysis.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

// REQ-011-14: the nodes some restraint holds, sorted and unique.
std::vector<core::NodeIndex> supportedNodes(const core::Model& model, const core::Mesh& mesh)
{
    std::vector<core::NodeIndex> nodes;
    for (const core::Restraint& restraint : model.restraints) {
        if (auto resolved = core::resolve(mesh, restraint.nodes)) {
            nodes.insert(nodes.end(), resolved->begin(), resolved->end());
        }
    }
    std::ranges::sort(nodes);
    const auto [first, last] = std::ranges::unique(nodes);
    nodes.erase(first, last);
    return nodes;
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
                                    .warnings = outcome.issues,
                                    .supported = mesh.mesh ? supportedNodes(built.model, *mesh.mesh)
                                                           : std::vector<core::NodeIndex>{}};
    return outcome;
}

namespace {

ItemId itemOf(const std::shared_ptr<const AnalysisResult>& result)
{
    return result->item;
}

} // namespace

void ResultSet::put(AnalysisResult result)
{
    const ItemId item = result.item;
    auto shared = std::make_shared<const AnalysisResult>(std::move(result));
    const auto at = std::ranges::lower_bound(m_results, item, {}, itemOf);
    if (at != m_results.end() && (*at)->item == item) {
        *at = std::move(shared);
    } else {
        m_results.insert(at, std::move(shared));
    }
}

void ResultSet::erase(ItemId item)
{
    const auto at = std::ranges::lower_bound(m_results, item, {}, itemOf);
    if (at != m_results.end() && (*at)->item == item) {
        m_results.erase(at);
    }
}

std::shared_ptr<const AnalysisResult> ResultSet::share(ItemId item) const
{
    const auto at = std::ranges::lower_bound(m_results, item, {}, itemOf);
    return at != m_results.end() && (*at)->item == item ? *at : nullptr;
}

const AnalysisResult* ResultSet::find(ItemId item) const
{
    return share(item).get();
}

namespace {

// Keeps the value of largest magnitude; equal ones by the smaller id.
void keep(std::optional<Extreme>& best, double value, core::ExternalId id, int direction)
{
    if (!std::isfinite(value)) {
        return;
    }
    constexpr double kEqual = 1e-12;
    const double size = std::abs(value);
    if (best) {
        const double was = std::abs(best->value);
        const bool larger = size > was * (1.0 + kEqual);
        const bool tie = !larger && size >= was * (1.0 - kEqual) && id < best->id;
        if (!larger && !tie) {
            return;
        }
    }
    best = Extreme{.value = value, .id = id, .direction = direction};
}

void nodal(std::optional<Extreme>& best, const std::vector<Eigen::Vector3d>& values,
           const core::Mesh& mesh)
{
    for (std::size_t n = 0; n < values.size(); ++n) {
        const core::ExternalId id = mesh.nodeId(static_cast<core::NodeIndex>(n));
        for (int d = 0; d < 3; ++d) {
            keep(best, values[n][d], id, d);
        }
    }
}

void staticPart(Summary& summary, const core::StaticResult& r, const core::Mesh* analysed)
{
    const core::Mesh* mesh = r.mesh ? r.mesh.get() : analysed;
    if (mesh == nullptr) {
        return;
    }
    nodal(summary.displacement, r.displacements, *mesh);
    nodal(summary.rotation, r.rotations, *mesh);
    for (const Eigen::Vector3d& reaction : r.reactions) {
        summary.reactions += reaction;
    }
    summary.applied = r.appliedForce;
    for (std::size_t e = 0; e < r.beams.size(); ++e) {
        const core::ExternalId id = mesh->elementId(static_cast<core::ElementIndex>(e));
        if (const auto& beam = r.beams[e]) {
            for (const core::BeamForces* f : {&beam->start(), &beam->end()}) {
                const std::array<double, 6> values{f->n, f->vy, f->vz, f->t, f->my, f->mz};
                for (std::size_t k = 0; k < values.size(); ++k) {
                    keep(summary.barForces[k], values[k], id, static_cast<int>(k));
                }
            }
        } else if (e < r.axialForce.size()) {
            keep(summary.barForces[0], r.axialForce[e], id, 0); // a truss: N alone
        }
    }
}

} // namespace

Summary summarize(const AnalysisResult& result)
{
    Summary summary;
    if (const auto* linear = std::get_if<core::StaticResult>(&result.data)) {
        staticPart(summary, *linear, result.mesh.get());
    } else if (const auto* second = std::get_if<core::SecondOrderResult>(&result.data)) {
        staticPart(summary, second->result, result.mesh.get());
        summary.iterations = second->iterations;
        summary.change = second->change;
    } else if (const auto* buckling = std::get_if<core::BucklingResult>(&result.data)) {
        for (const core::BucklingMode& mode : buckling->modes) {
            summary.factors.push_back(mode.factor);
        }
        summary.note = buckling->note;
    }
    return summary;
}

} // namespace fem::setup
