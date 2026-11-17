#pragma once

// The stages of the static analyses (design 008c §3, §5.5): the linear
// analysis of the 007 to 008b, and the second order and the buckling of the
// 008c, which assemble the same problem many times with the axial forces of
// a reference state.

#include "detail/model_validation.hpp"

#include <fem/core/assembler.hpp>
#include <fem/core/beam_element.hpp>
#include <fem/core/beam_iso.hpp>
#include <fem/core/beam_loads.hpp>
#include <fem/core/beam_state.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/static_analysis.hpp>
#include <fem/core/truss_element.hpp>

#include <Eigen/Core>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fem::core::detail {

// The beams of the model, per element (design 008 §5.6): data, span loads
// and flexible length; default values for elements that are not beams.
struct Beams
{
    std::vector<BeamData> data;
    std::vector<SpanLoads> loads;
    std::vector<double> length;
    std::vector<bool> exact; // the exact element of 008b (design 008b §5.3)
    // The isoparametric curved beams (design 008b §5.5).
    std::vector<IsoBeamData> iso;
    std::vector<bool> isIso;
};

// A validated model ready to assemble and solve. The formulations refer to
// the data it holds, so it is neither copied nor moved.
class StaticProblem
{
public:
    // Validates the model (failing with its issues, one per line) and sets
    // up the bars, the beams, the dofs and the nodal loads. With
    // `secondOrder` (feature 008c) the straight beams use the exact element,
    // which takes the reference axial force.
    [[nodiscard]] static Expected<std::unique_ptr<StaticProblem>>
    prepare(const Model& model, const StaticOptions& options, bool secondOrder);

    StaticProblem(const StaticProblem&) = delete;
    StaticProblem& operator=(const StaticProblem&) = delete;
    StaticProblem(StaticProblem&&) = delete;
    StaticProblem& operator=(StaticProblem&&) = delete;
    ~StaticProblem();

    [[nodiscard]] const Mesh& mesh() const { return *m_model.mesh; }
    [[nodiscard]] const DofMap& dofs() const { return m_dofs; }

    // The matrix and the load vector on the free dofs; the negative pivots
    // of the elements' internal condensations go to negativePivots().
    [[nodiscard]] Expected<AssembledSystem> assemble() const;
    // REQ-007-15: free dofs without stiffness, all of them.
    [[nodiscard]] Expected<void> checkDiagonal(const SparseMatrix& matrix) const;
    // "the node 12 in v" for an equation of the system.
    [[nodiscard]] std::string equationLabel(std::int32_t equation) const;
    // The values of all dofs from those of the free ones; the prescribed
    // ones at their values, or at zero (a buckling mode).
    [[nodiscard]] Eigen::VectorXd valuesOf(const Eigen::VectorXd& free,
                                           bool prescribed = true) const;
    // Displacements, reactions and the forces of every bar; without
    // `loaded`, without the loads (the shape of a buckling mode).
    [[nodiscard]] Expected<StaticResult> recover(const Eigen::VectorXd& values,
                                                 bool loaded = true) const;

    // Feature 008c: the sum of the negative pivots of the elements in the
    // last assembly, and the element with the most (-1 if none).
    [[nodiscard]] std::int64_t negativePivots() const;
    [[nodiscard]] ElementIndex unstableElement() const;

    // Feature 008c (design 008c §5.2): the axial force of every bar of the
    // solution `values`, per element (inactive for the others), and the
    // reference axial forces of the next assemblies.
    [[nodiscard]] std::vector<AxialProfile> profilesOf(const Eigen::VectorXd& values) const;
    void setAxialForces(const std::vector<AxialProfile>& profiles);

    // Feature 008c: the negative pivots of each element in the last
    // assembly, and the shape of an exact bar buckling with its nodes held
    // at the present axial forces, with its largest displacement 1 (design
    // 008c §5.6); nothing for other elements.
    [[nodiscard]] const std::vector<int>& elementNegativePivots() const { return m_negative; }
    [[nodiscard]] std::optional<BeamResult> internalMode(ElementIndex element) const;

private:
    StaticProblem(const Model& model, const StaticOptions& options);
    // The values of the element's dofs, node by node in its order.
    [[nodiscard]] std::vector<double> nodeValues(ElementIndex element, int nodes,
                                                 const Eigen::VectorXd& values) const;

    const Model& m_model;
    StaticOptions m_options;
    Validation m_validation;
    std::vector<BarData> m_bars;
    Beams m_beams;
    std::vector<ElementKind> m_kindOf;
    std::array<std::vector<ElementIndex>, 4> m_members; // in ElementKind order
    std::vector<std::vector<ElementIndex>> m_exactMembers;
    std::array<std::vector<ElementIndex>, 2> m_isoMembers; // 2D, 3D
    std::vector<std::unique_ptr<ElementFormulation>> m_formulations;
    std::vector<ElementSet> m_sets;
    DofMap m_dofs;
    std::vector<NodalLoad> m_loads;
    std::vector<DofIndex> m_dofOfEquation;
    mutable std::vector<int> m_negative; // per element, written by the exact elements
};

} // namespace fem::core::detail
