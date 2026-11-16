#pragma once

// The validation of a model (REQ-007-09, 10, 14; REQ-008-24), shared by
// validate() and runLinearStatic(), and what both need to build the beams.

#include <fem/core/beam_element.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/model.hpp>
#include <fem/core/section.hpp>
#include <fem/core/static_analysis.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace fem::core::detail {

inline constexpr int kNoAssignment = -1;
inline constexpr int kNoItem = -1;

// What validation learns about the model, reused by the analysis.
struct ValidationTables
{
    std::vector<int> owner;        // per element: assignment, or kNoAssignment
    std::vector<DofMask> nodeDofs; // per node, from the valid bars
    // Per element: the item of the model that applies, or kNoItem.
    std::vector<int> offsetOf;
    std::vector<int> foundationOf;
    std::vector<std::array<int, 2>> connectionOf; // start, end
    bool hasBeams = false;
};

struct Validation
{
    ValidationTables tables;
    std::vector<ModelIssue> issues;
};

// Every problem of a model that has a mesh, in model order.
[[nodiscard]] Validation validateModel(const Model& model);

// The dofs an element kind gives to its nodes.
[[nodiscard]] DofMask dofsOf(ElementKind kind);

[[nodiscard]] bool isBeam(ElementKind kind);

[[nodiscard]] std::string quoted(std::string_view name);

// The section area that a bar uses (shape or generic).
[[nodiscard]] double sectionArea(const Section& section);

// A beam resolved from the model: its data without the span loads, the
// frame of the loads and the flexible length.
struct BeamSetup
{
    BeamData data;
    SpanFrame frame;
    double length = 0.0;
};

// For an element whose assignment, material and section are valid; fails
// for a reference parallel to the bar or no flexible length.
[[nodiscard]] Expected<BeamSetup> beamSetup(const Model& model, ElementIndex element,
                                            const ValidationTables& tables);

} // namespace fem::core::detail
