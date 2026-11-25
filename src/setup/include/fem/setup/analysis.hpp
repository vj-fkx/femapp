#pragma once

// Feature 011: the analysis of a load case or combination of the document
// (design 011 §4, §5.4; REQ-011-02, 05, 10).

#include <fem/core/expected.hpp>
#include <fem/core/mesh.hpp>
#include <fem/core/run_control.hpp>
#include <fem/core/stability_analysis.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/geom/drawing.hpp>
#include <fem/setup/analysis_settings.hpp>
#include <fem/setup/build.hpp>
#include <fem/setup/document.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace fem::setup {

// What was analysed: the result is current while these are the state's
// (REQ-011-12).
struct AnalysedState
{
    std::uint64_t documentRevision = 0;
    std::uint64_t drawingRevision = 0;
    std::uint64_t meshGeneration = 0;
    friend bool operator==(const AnalysedState&, const AnalysedState&) = default;
};

using CoreResult = std::variant<core::StaticResult, core::SecondOrderResult, core::BucklingResult>;

struct AnalysisResult
{
    ItemId item{};
    AnalysisSettings settings;
    AnalysedState state;
    std::shared_ptr<const core::Mesh> mesh;
    CoreResult data;
    std::vector<BuildIssue> warnings; // REQ-011-05
};

// REQ-011-02, 05, 10: one case or combination: build (010), check, run.
// Errors of the check refuse it (the issues come back, nothing runs); a
// failure or a cancellation of the core comes back as its Error.
struct CaseOutcome
{
    std::optional<AnalysisResult> result;
    std::vector<BuildIssue> issues; // the check: errors, or the warnings of a result
    std::optional<core::Error> error;
};

[[nodiscard]] CaseOutcome runCase(const Document& document, const geom::Drawing& drawing,
                                  const MeshContext& mesh, ItemId item,
                                  const core::RunControl& control);

} // namespace fem::setup
