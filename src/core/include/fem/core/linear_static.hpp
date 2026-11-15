#pragma once

#include <fem/core/assembler.hpp>
#include <fem/core/dof_map.hpp>
#include <fem/core/element_formulation.hpp>
#include <fem/core/expected.hpp>
#include <fem/core/linear_solver.hpp>
#include <fem/core/mesh.hpp>

#include <Eigen/Core>
#include <span>

namespace fem::core {

// REQ-006-17
struct LinearSolution
{
    Eigen::VectorXd values;    // per DofIndex: free and prescribed
    Eigen::VectorXd reactions; // per DofIndex: nonzero only at prescribed dofs
};

// Assembles, solves and recovers the reactions of a linear problem.
[[nodiscard]] Expected<LinearSolution> solveLinear(const Mesh& mesh, const DofMap& dofs,
                                                   std::span<const ElementSet> sets,
                                                   std::span<const NodalLoad> loads,
                                                   LinearSolver& solver, int threads = 0);

} // namespace fem::core
